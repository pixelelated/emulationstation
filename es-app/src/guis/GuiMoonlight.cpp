#include "guis/GuiMoonlight.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#include "ApiSystem.h"
#include "Scripting.h"
#include "Window.h"
#include "guis/GuiMsgBox.h"
#include "utils/Platform.h"
#include "Log.h"
#include "SystemConf.h"
#include "HttpReq.h"
#include "utils/AtomicFileUtil.h"
#include "utils/StringUtil.h"
#include <pugixml.hpp>

static const std::string MOONLIGHT_CONF_DIR = "/storage/.config/Moonlight Game Streaming Project";
static const std::string MOONLIGHT_CACHE_DIR = "/storage/.cache/Moonlight Game Streaming Project";
static const std::string MOONLIGHT_CONF = MOONLIGHT_CONF_DIR + "/Moonlight.conf";
static const std::string MOONLIGHT_ROMS = "/storage/roms/moonlight";

class MoonlightClient {
 public:
  MoonlightClient(const std::string& server_ip)
   : server_ip_(server_ip) {}
  
  // Requests to Moonlight server
  std::string GetAppListXml() { return MakeRequest(MakeUrl("applist")); }
  void QuitApp() { MakeRequest(MakeUrl("cancel")); }
  bool WriteBoxArtFile(const std::string& app_id, std::string filename);

  // Helpers
  bool UpdateMoonlightGames(bool* notSaved);

 private:
  std::string MakeUrl(const std::string& command, std::string args={});
  std::string MakeRequest(const std::string& url, std::string* filename = nullptr, bool* ok = nullptr);
  static std::string CreateNewGuid();

  std::string server_ip_;
};

// Moonlight version check function: reads the probe's output to its end and
// closes it (#1249: it streamed the FILE pointer itself, which never says
// Embedded, and never closed the pipe)
bool isEmbedded(void) {
  FILE* mlver = popen("moonlight -v", "r");
  if (mlver == nullptr) return false;
  std::string output;
  char buff[256];
  while (fgets(buff, sizeof buff, mlver) != nullptr)
    output += buff;
  pclose(mlver);
  return output.find("Embedded") != std::string::npos;
}

// SSL extraction (client.pem, key.pem) from Moonlight.conf's line
// `<marker>@ByteArray(...)`, the value quoted or not. Bounded (#1241): a conf
// without the line, or a value not of that shape, is false and writes
// nothing, where the loops ran past the end of the file or replaced at npos;
// the pem is written whole or not at all, with `mode` (#1250: the key 0600).
static bool extractPem(const std::string& marker, const std::string& target, int mode) {
  std::ifstream confFile(MOONLIGHT_CONF);
  std::string line;
  bool found = false;
  while (std::getline(confFile, line)) {
    if (line.compare(0, marker.size(), marker) == 0) {
      found = true;
      break;
    }
  }
  if (!found) return false;

  std::string value = line.substr(marker.size());
  if (!value.empty() && value.back() == '\r') value.pop_back();
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.substr(1, value.size() - 2);
  const std::string open = "@ByteArray(";
  if (value.size() <= open.size() + 1 || value.compare(0, open.size(), open) != 0 || value.back() != ')')
    return false;
  value = value.substr(open.size(), value.size() - open.size() - 1);

  std::string pem;
  std::string::size_type pos = 0;
  while (pos < value.size()) {
    std::string::size_type next = value.find("\\n", pos);
    if (next == std::string::npos) {
      pem += value.substr(pos);
      break;
    }
    pem += value.substr(pos, next - pos) + "\n";
    pos = next + 2;
  }
  if (pem.empty()) return false;

  mkdir(MOONLIGHT_CACHE_DIR.c_str(), 0755);
  return Utils::AtomicFile::writeText(target, pem, mode);
}

// Remove a tree by its literal path and say whether it is gone (#1240: the
// quoted "~/..." was never expanded, so nothing was removed and the box said
// it had been)
static bool removeTree(const std::string& path) {
  Utils::Platform::runSystemCommand("rm -r " + Utils::String::shellQuote(path), "", nullptr);
  struct stat st;
  return lstat(path.c_str(), &st) != 0 && errno == ENOENT;
}

// A launcher's file name keeps only what a path and a shell read as
// themselves (#1238: only '/' was replaced); bytes past ASCII are kept, so a
// title in another script keeps its name
static std::string launcherName(const std::string& title, std::set<std::string>& used) {
  std::string name;
  for (unsigned char c : title)
    name += (isalnum(c) || c >= 0x80 || c == ' ' || c == '.' || c == '_' || c == '-' || c == '+' || c == ',') ? (char)c : ' ';
  name = Utils::String::trim(name);
  while (!name.empty() && name[0] == '.') name.erase(0, 1);
  if (name.empty()) name = "Moonlight game";
  std::string unique = name;
  for (int n = 2; used.count(unique); n++) unique = name + " " + std::to_string(n);
  used.insert(unique);
  return unique;
}

// Whether the original's `rm moonlight/*` and `rm moonlight/images/*` kept
// this entry of `dir`: a dot entry or a directory (images excepted at the top)
static bool keptByUpdate(const std::string& dir, const std::string& name, bool top) {
  if (name == "." || name == "..") return false;
  if (name[0] == '.') return true;
  struct stat st;
  if (lstat((dir + "/" + name).c_str(), &st) != 0) return true;
  return S_ISDIR(st.st_mode) && !(top && name == "images");
}

// What the update kept is carried from the set put aside into the new one;
// what it replaced is unlinked; a name that cannot be carried stays where it
// is, and so does the set put aside, never removed with it
static void retireOldSet(const std::string& aside) {
  for (const std::string sub : { std::string(""), std::string("/images") }) {
    DIR* d = opendir((aside + sub).c_str());
    if (d == nullptr) continue;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) names.push_back(e->d_name);
    closedir(d);
    for (const auto& name : names) {
      if (name == "." || name == "..") continue;
      std::string from = aside + sub + "/" + name;
      if (keptByUpdate(aside + sub, name, sub.empty()))
        rename(from.c_str(), (MOONLIGHT_ROMS + sub + "/" + name).c_str());
      else if (!(sub.empty() && name == "images"))
        unlink(from.c_str());
    }
  }
  rmdir((aside + "/images").c_str());
  rmdir(aside.c_str());
}

// File existence check function
bool fileExists (const std::string& name) {
  if (FILE *file = fopen(name.c_str(), "r")) {
      fclose(file);
      return true;
  } else {
      return false;
  }   
}

std::string MoonlightClient::CreateNewGuid() {
  srand(time(NULL));

  char strUuid[256];
  snprintf(strUuid, sizeof strUuid, "%x%x-%x-%x-%x-%x%x%x", 
      rand(), rand(),                 // Generates a 64-bit Hex number
      rand(),                         // Generates a 32-bit Hex number
      ((rand() & 0x0fff) | 0x4000),   // Generates a 32-bit Hex number of the form 4xxx (4 indicates the UUID version)
      rand() % 0x3fff + 0x8000,       // Generates a 32-bit Hex number in the range [0x8000, 0xbfff]
      rand(), rand(), rand());
  return strUuid;
}

std::string MoonlightClient::MakeUrl(
    const std::string& command, std::string args) {
  if (!args.empty()) args += "&";
  args += "uniqueid=0123456789ABCDEF&uuid=" + CreateNewGuid();

  char url[1024];
  snprintf(url, sizeof url, "https://%s:47984/%s?%s",
      server_ip_.c_str(), command.c_str(), args.c_str());
  return std::string(url);
}

std::string MoonlightClient::MakeRequest(const std::string& url, std::string* filename, bool* ok) {
  std::string cert_path;
  if (fileExists(MOONLIGHT_CONF) == true) {
    cert_path = MOONLIGHT_CACHE_DIR + "/";
  } else {
    cert_path = "/storage/.cache/moonlight/";
  }

  HttpReqOptions opts;
  opts.clientCert = cert_path + "client.pem";
  opts.clientKey = cert_path + "key.pem";

  if (filename != nullptr)
    opts.outputFilename = *filename;

	HttpReq req(url, &opts);
  std::cout << "MoonlightClient::MakeRequest : " << url << std::endl;
	req.wait();
	
  if (ok) *ok = req.status() == HttpReq::REQ_SUCCESS;
	if (req.status() != HttpReq::REQ_SUCCESS) return "";
  if (filename) {
    std::cout << "MoonlightClient::MakeRequest saved to : " << *filename << std::endl;
    return {};
  } else {
    auto res = req.getContent();
    std::cout << "MoonlightClient::MakeRequest got : " << res << std::endl;
    return res;
  }
}

bool MoonlightClient::WriteBoxArtFile(const std::string& app_id, std::string filename) {
  char args[256];
  snprintf(args, sizeof args, "appid=%s&AssetType=2&AssetIdx=0", app_id.c_str());
  bool ok = false;
  MakeRequest(MakeUrl("appasset", args), &filename, &ok);
  return ok;
}

// The list is fetched and parsed before anything is touched, and the new set
// -- launchers, images, gamelist.xml -- is built beside the old one and
// replaces it only whole (#1239: the library was removed first, so a failed
// fetch left it empty, and a write that failed still answered true). False
// with *notSaved false is the server's; with *notSaved true, a write's. On
// false the library is as it was.
bool MoonlightClient::UpdateMoonlightGames(bool* notSaved) {
  *notSaved = false;

  auto xml = GetAppListXml();
  if (xml.empty()) return false;

	pugi::xml_document doc;
	auto result = doc.load_string(xml.c_str());
 	if (!result) return false;

  // list api returns the following xml
  // <?xml version="1.0" encoding="UTF-16"?>
  // <root protocol_version="0.1" query="applist" status_code="200" status_message="OK">
  //   <App>
  //     <AppInstallPath>C:\Program Files (x86)\Steam\</AppInstallPath>
  //     <AppTitle>Steam</AppTitle>
  //     <CmsId>100021711</CmsId>
  //     <Distributor>Steam</Distributor>
  //     <ID>1088017781</ID>
  //     <IsAppCollectorGame>0</IsAppCollectorGame>
  //     <IsHdrSupported>1</IsHdrSupported>
  //     <MaxControllersForSingleSession>1</MaxControllersForSingleSession>
  //     <ShortName>steam</ShortName>
  //     <SupportedSOPS>
  //       <SOPS>
  //         <Height>2160</Height>
  //         <RefreshRate>60</RefreshRate>
  //         <Width>3840</Width>
  //       </SOPS>
  //       ...
  //     </SupportedSOPS>
  //     <UniqueId>20225001</UniqueId>
  //     <simulateControllers>0</simulateControllers>
  //   </App>
  //   ...
  // </root>

	pugi::xml_node root = doc.child("root");
  if (!root) return false;

  const std::string staging = MOONLIGHT_ROMS + ".update";
  const std::string aside = MOONLIGHT_ROMS + ".previous";
  *notSaved = true;
  if (!removeTree(staging) || mkdir(staging.c_str(), 0755) != 0 || mkdir((staging + "/images").c_str(), 0755) != 0) {
    removeTree(staging);
    return false;
  }
  const bool qt = fileExists(MOONLIGHT_CONF);

  pugi::xml_document gamelist_doc;
  auto gamelist = gamelist_doc.append_child("gameList");
  std::set<std::string> used;

	for (auto app : root.children())
	{
    if (std::string(app.name()) != "App") continue;

    std::string title = app.child("AppTitle").text().get();
    std::cout << "MoonlightClient::UpdateMoonlightGames: " << title << std::endl;
    auto filename = launcherName(title, used);

    // Write bash script; every value quoted (#1238)
    std::ofstream app_file(staging + "/" + filename + ".sh");
    app_file << "#!/bin/sh" << std::endl;
    app_file << "/usr/bin/controller-layout moonlight " << Utils::String::shellQuote(filename + ".sh") << " /storage/.config/moonlight/gamecontrollerdb.txt" << std::endl;
    if (qt)
      app_file << "QT_QPA_PLATFORM=wayland moonlight stream " << Utils::String::shellQuote(server_ip_) << " " << Utils::String::shellQuote(title) << " --quit-after" << std::endl;
    else
      app_file << "moonlight stream -app " << Utils::String::shellQuote(title) << " -platform sdl " << Utils::String::shellQuote(server_ip_) << std::endl;
    app_file.close();
    if (!app_file) {
      removeTree(staging);
      return false;
    }

    // Write box art image; one that does not arrive leaves the game without
    // an image, where the list named a file that was never written
    std::string app_id = app.child("ID").text().get();
    const std::string image = staging + "/images/" + filename + ".png";
    bool art = !app_id.empty() && WriteBoxArtFile(app_id, image);
    if (!art) unlink(image.c_str());

    // Update gamelist
    auto game = gamelist.append_child("game");
    game.append_child("path").text().set(std::string("./" + filename + ".sh").c_str());
    game.append_child("name").text().set(title.c_str());
    if (art)
      game.append_child("image").text().set(std::string("./images/" + filename + ".png").c_str());
  }

  if (!gamelist_doc.save_file((staging + "/gamelist.xml").c_str())) {
    removeTree(staging);
    return false;
  }

  // The swap: the old set aside, the new one in, the old one retired. A set
  // an earlier run left aside is retired first, never removed whole: what it
  // still holds is something that run could not carry
  struct stat st;
  bool hadLibrary = lstat(MOONLIGHT_ROMS.c_str(), &st) == 0;
  if (lstat(aside.c_str(), &st) == 0) retireOldSet(aside);
  if (lstat(aside.c_str(), &st) == 0 || (hadLibrary && rename(MOONLIGHT_ROMS.c_str(), aside.c_str()) != 0)) {
    removeTree(staging);
    return false;
  }
  if (rename(staging.c_str(), MOONLIGHT_ROMS.c_str()) != 0) {
    if (hadLibrary) rename(aside.c_str(), MOONLIGHT_ROMS.c_str());
    removeTree(staging);
    return false;
  }
  if (hadLibrary) retireOldSet(aside);

  *notSaved = false;
  return true;
}

///////////////////////////////////////////////////////////////////////////////

void GuiMoonlight::show(Window* window)
{
	window->pushGui(new GuiMoonlight(window));
}

GuiMoonlight::GuiMoonlight(Window* window)
 : GuiSettings(window, _("MOONLIGHT GAME STREAMING"))
{
  char pin[5];
  snprintf(pin, sizeof pin, "%04d", rand() % 10000);

  // A key extracted before #1250 was made 0644 under the usual umask; it is
  // narrowed here, and written 0600 from now on
  chmod((MOONLIGHT_CACHE_DIR + "/key.pem").c_str(), 0600);

	auto theme = ThemeData::getMenuTheme();
	std::shared_ptr<Font> font = theme->Text.font;
	unsigned int color = theme->Text.color;
	auto pinUI = std::make_shared<TextComponent>(window, pin, font, color);

	addGroup(_("TOOLS"));

  // Every value handed to the shell is quoted (#1238): the SERVER IP is what
  // the player typed
  addEntry(_("QUIT CURRENT GAME"), false, [window] {
    std::string server_ip = SystemConf::getInstance()->get("moonlight.host");
    ApiSystem::executeScriptLegacy("moonlight quit " + Utils::String::shellQuote(server_ip));
  });

  addEntry(_("UPDATE MOONLIGHT GAMES"), false, [window] {
    std::string server_ip = SystemConf::getInstance()->get("moonlight.host");
    
    MoonlightClient client(server_ip);
    bool notSaved = false;
    if (!server_ip.empty() && client.UpdateMoonlightGames(&notSaved)) {
      Scripting::fireEvent("quit", "restart");
			Utils::Platform::quitES(Utils::Platform::QuitMode::QUIT);
    } else if (notSaved) {
      window->pushGui(new GuiMsgBox(window, _("The game list couldn't be saved. Your Moonlight games are as they were.")));
    } else {
      window->pushGui(new GuiMsgBox(window, _("Unable to connect to server")));
    }
  });

  addEntry(_("PAIR WITH SERVER"), false, [window, pin] {
    std::string server_ip = SystemConf::getInstance()->get("moonlight.host");

    std::string cmd = "moonlight pair -pin " + Utils::String::shellQuote(pin) + " " + Utils::String::shellQuote(server_ip);
    if (isEmbedded() == false)
      cmd = "QT_QPA_PLATFORM=wayland " + cmd;
    bool paired = false;
		ApiSystem::executeScriptLegacy(cmd, [server_ip, &paired](std::string line) {
      std::string new_server_ip;
      if (ParseServerIp(line, &new_server_ip) && server_ip != new_server_ip) {
  			SystemConf::getInstance()->set("moonlight.host", new_server_ip);
      }
      // moonlight's own line, spelt as moonlight prints it
      if (line == "Succesfully paired")
        paired = true;
		});

    // The pems the requests use: a new pairing replaces them, a missing one is
    // extracted, once the pairing has ended; a conf that cannot be read says
    // so (#1241: the parsers hung on it or threw)
    bool extracted = true;
    if (fileExists(MOONLIGHT_CONF)) {
      const std::string cert_path = MOONLIGHT_CACHE_DIR + "/";
      if (paired || !fileExists(cert_path + "client.pem"))
        extracted = extractPem("certificate=", cert_path + "client.pem", -1) && extracted;
      if (paired || !fileExists(cert_path + "key.pem"))
        extracted = extractPem("key=", cert_path + "key.pem", 0600) && extracted;
    }
    if (!extracted)
      window->pushGui(new GuiMsgBox(window, _("The pairing file couldn't be read. Try pairing again.")));
    else if (paired)
      window->pushGui(new GuiMsgBox(window, _("Successfully paired with server")));
	});

  // The removals by the literal paths, each one checked (#1240); the cache
  // first, so a removal that fails leaves the pairing whole
  addEntry(_("UNPAIR WITH SERVER"), false, [this, window] {
		if (fileExists(MOONLIGHT_CONF) == true) {
      if (removeTree(MOONLIGHT_CACHE_DIR) && removeTree(MOONLIGHT_CONF_DIR))
        window->pushGui(new GuiMsgBox(window, _("Unpaired and settings cleared")));
      else
        window->pushGui(new GuiMsgBox(window, _("The pairing files couldn't be removed. This device is still paired.")));
    } else {
      if (removeTree("/storage/.cache/moonlight"))
        window->pushGui(new GuiMsgBox(window, _("Unpaired from server")));
      else
        window->pushGui(new GuiMsgBox(window, _("The pairing files couldn't be removed. This device is still paired.")));
    }
	});

	addGroup(_("SETTINGS"));
  addInputTextConfigRow(_("SERVER IP"), "moonlight.host", false);
  addWithLabel(_("PAIRING PIN"), pinUI);
}

std::vector<std::string> GuiMoonlight::ParseAppList(const std::vector<std::string>& vec) {
  std::vector<std::string> apps;
  for (auto line : vec) {
    int pos = line.find(". ");
    if (pos == -1) continue;
    apps.push_back(line.substr(pos+2));
  }
  return apps;
}

bool GuiMoonlight::ParseServerIp(const std::string& line, std::string* server_ip) {
  // Watching for "Connect to 10.1.10.217..." line.
  const std::string prompt = "Connect to ";
  if (line.find(prompt) != 0) return false;

  const std::string ip = line.substr(prompt.length());
  if (ip.substr(ip.length() - 3) != "...") return false;

  *server_ip = ip.substr(0, ip.length() - 3);
  return true;
}
