#include "WebImageComponent.h"
#include "utils/StringUtil.h"
#include "HttpReq.h"
#include "Log.h"
#include "resources/TextureResource.h"
#include "Paths.h"
#include "utils/OfflineProxyUrl.h"
#include "utils/md5.h"

#include <algorithm>
#include <cctype>
#include <functional>

struct url
{
public:
	static url parse(const std::string& url_s)
	{
		url ret;

		using namespace std;

		const std::string prot_end("://");
		std::string::const_iterator prot_i = search(url_s.begin(), url_s.end(), prot_end.begin(), prot_end.end());
		ret.protocol.reserve(distance(url_s.begin(), prot_i));
		std::transform(url_s.begin(), prot_i, std::back_inserter(ret.protocol), [](unsigned char c) { return std::tolower(c); });
		if (prot_i == url_s.end())
			return ret;

		advance(prot_i, prot_end.length());
		string::const_iterator path_i = find(prot_i, url_s.end(), '/');
		ret.host.reserve(distance(prot_i, path_i));
		std::transform(prot_i, path_i, std::back_inserter(ret.host), [](unsigned char c) { return std::tolower(c); }); // host is icase
		string::const_iterator query_i = find(path_i, url_s.end(), '?');
		ret.path.assign(path_i, query_i);
		if (query_i != url_s.end())
			++query_i;
		ret.query.assign(query_i, url_s.end());
		return ret;
	}

	std::string protocol, host, path, query;
};

// #1323: a cache file is named by the digest of its whole URL, directly in the
// cache directory, with the URL's extension kept only when it is one to five
// letters or digits. Every file this component removes, reads or has written
// is checked to sit directly in the cache directory first (containment); a
// name that fails the check is refused and nothing is touched.
static std::string cacheDirectory()
{
	return Utils::FileSystem::getGenericPath(Paths::getUserEmulationStationPath() + "/tmp");
}

static bool insideCacheDirectory(const std::string& file, const std::string& cacheDir)
{
	if (file.empty() || cacheDir.empty())
		return false;

	auto dir = Utils::FileSystem::getCanonicalPath(cacheDir);
	return !dir.empty() && Utils::FileSystem::getParent(Utils::FileSystem::getCanonicalPath(file)) == dir;
}

static std::string cacheNameFor(const std::string& address, const std::string& cacheDir)
{
	std::string name = url::parse(address).path;
	auto slash = name.find_last_of('/');
	if (slash != std::string::npos)
		name = name.substr(slash + 1);

	std::string ext;
	auto dot = name.find_last_of('.');
	if (dot != std::string::npos)
	{
		ext = name.substr(dot + 1);
		bool plain = !ext.empty() && ext.size() <= 5 && std::all_of(ext.begin(), ext.end(), [](char c)
			{ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); });
		if (!plain)
			ext.clear();
	}

	std::string file = cacheDir + "/" + md5(address) + (ext.empty() ? "" : "." + ext);
	return insideCacheDirectory(file, cacheDir) ? file : "";
}

// keepInCacheDuration = 0 -> image expire immediately
// keepInCacheDuration = -1 -> image expire never
// keepInCacheDuration = 600 -> image expire in 10 minutes ( 60*10 )
// keepInCacheDuration = 86400 -> image expire in 24 hours ( 60*60*10 )

WebImageComponent::WebImageComponent(Window* window, double keepInCacheDuration) : ImageComponent(window, false), 
	mRequest(nullptr), mKeepInCacheDuration(keepInCacheDuration), mMaxSize(MaxSizeInfo::Empty),
	mBusyAnim(nullptr)
{
	mWaitLoaded = false;
	mOnLoaded = nullptr;
}

void WebImageComponent::resize()
{
	ImageComponent::resize();
	
	if (mBusyAnim != nullptr)
	{
		if (mSize.x() == 0)
			mBusyAnim->setSize(mTargetSize);
		else
			mBusyAnim->setSize(mSize);
	}

	if (getParent() != nullptr && getParent()->isKindOf<ComponentGrid>())
		getParent()->onSizeChanged();
}

WebImageComponent::~WebImageComponent()
{
	if (mRequest != nullptr)
		delete mRequest;

	if (mBusyAnim != nullptr)
		delete mBusyAnim;
	
	// The cache is flat since #1323, so there is no directory to walk up.
	if (mKeepInCacheDuration == 0 && insideCacheDirectory(mLocalFile, cacheDirectory()) && Utils::FileSystem::exists(mLocalFile))
		Utils::FileSystem::removeFile(mLocalFile);
}

void WebImageComponent::setImage(const std::string& path, bool tile, const MaxSizeInfo& maxSize, bool checkFileExists, bool allowMultiImagePlaylist)
{
	if (mRequest != nullptr)
	{
		delete mRequest;
		mRequest = nullptr;
	}

	// A rebind starts with nothing queued: a URL left from the last bind
	// would otherwise be fetched and drawn over a local or cached image
	// (#1327). The network path below queues its own.
	mUrlToLoad = "";
	mLocalFile = "";

	mWaitLoaded = true;

	if (!Utils::String::startsWith(path, "http://") && !Utils::String::startsWith(path, "https://"))
	{
		if (path.empty())
			ImageComponent::setImage(nullptr, 0);
		else
		{
			ImageComponent::setImage(path, tile, maxSize, checkFileExists, allowMultiImagePlaylist);
			resize();
		}
		return;
	}

	url uri = url::parse(path);
	if (uri.host.empty() || uri.path.empty())
		return;

	std::string localFile = cacheNameFor(path, cacheDirectory());
	if (localFile.empty())
	{
		LOG(LogWarning) << "WebImageComponent: an image from " << uri.host << " has no cache file inside the cache directory; not loaded";
		mUrlToLoad = "";
		mLocalFile = "";
		ImageComponent::setImage(nullptr, 0);
		return;
	}

	if (Utils::FileSystem::exists(localFile))
	{
		bool keepLoadingLocal = true;

		if (mKeepInCacheDuration == 0)
		{
			keepLoadingLocal = false;
			Utils::FileSystem::removeFile(localFile);
		}
		else if (mKeepInCacheDuration > 0)
		{
			auto date = Utils::FileSystem::getFileCreationDate(localFile);
			auto duration = Utils::Time::DateTime::now().elapsedSecondsSince(date);
			if (duration > mKeepInCacheDuration)
			{
				keepLoadingLocal = false;
				Utils::FileSystem::removeFile(localFile);
			}
		}

		if (keepLoadingLocal)
		{
			ImageComponent::setImage(localFile, tile, maxSize, checkFileExists, allowMultiImagePlaylist);
			resize();
			return;
		}
	}

	mMaxSize = maxSize;
	mUrlToLoad = path;
	mLocalFile = localFile;
}

void WebImageComponent::update(int deltaTime)
{
	ImageComponent::update(deltaTime);

	if (mRequest == nullptr)
		return;

	if (mBusyAnim != nullptr)
		mBusyAnim->update(deltaTime);

	auto status = mRequest->status();
	if (status == HttpReq::REQ_IN_PROGRESS)
		return;

	if (mBusyAnim != nullptr)
	{
		delete mBusyAnim;
		mBusyAnim = nullptr;
	}

	delete mRequest;
	mRequest = nullptr;	

	if (status == HttpReq::REQ_SUCCESS && Utils::FileSystem::exists(mLocalFile))
	{
		// A 204, or a 200 with nothing in it, is a download that succeeded
		// with no image: the offline RetroAchievements proxy answers so for
		// a badge it has not cached yet (#180). Kept, an empty file would be
		// this component's copy for as long as the cache lasts -- forever by
		// default -- and the badge would stay blank after the proxy had it.
		if (Utils::FileSystem::getFileSize(mLocalFile) == 0)
		{
			// Not kept as done either: the component holds no cache path for
			// it, and the next bind asks again (#1319).
			Utils::FileSystem::removeFile(mLocalFile);
			mLocalFile = "";
			return;
		}

		ImageComponent::setImage(mLocalFile, false, mMaxSize, false);
		resize();
	}
}

void WebImageComponent::render(const Transform4x4f& parentTrans)
{
	if (!isVisible())
		return;

	Transform4x4f trans = parentTrans * getTransform();

	float xClipMargin = mSize.x() / 2.0f;
	int yClipMargin = mSize.y() / 2.0f;

	if (!Renderer::isVisibleOnScreen(
		trans.translation().x() - xClipMargin,
		trans.translation().y() - yClipMargin, 
		mSize.x() + 2 * xClipMargin,
		mSize.y() + 2 * yClipMargin))
		return;

	if (mRequest == nullptr && !mUrlToLoad.empty() && !mLocalFile.empty())
	{
		std::string localPath = Utils::FileSystem::getParent(mLocalFile);
		if (!Utils::FileSystem::exists(localPath))
			Utils::FileSystem::createDirectory(localPath);
		
		// An image on the offline RetroAchievements proxy is asked for from
		// its store and nothing else (fork #199): the icon or badge it has
		// cached, or a 404 at once. Without the header, a proxy that still
		// believes it is online -- up to fifteen seconds after a link drops,
		// or on a link that is up and leads nowhere -- fetches an image it
		// does not hold from the web with a ten-second timeout, one per
		// visible row, while the page shows the device's copy. Every other
		// host is asked exactly as before.
		HttpReqOptions options(mLocalFile);
		if (Utils::OfflineProxy::isProxyUrl(mUrlToLoad))
			options.customHeaders.push_back(Utils::OfflineProxy::StoreOnlyHeader);

		mRequest = new HttpReq(mUrlToLoad, &options);
		mUrlToLoad = "";

		if (mBusyAnim != nullptr)
			delete mBusyAnim;

		mBusyAnim = new BusyComponent(mWindow, "");
		mBusyAnim->setBackgroundVisible(false);
		mBusyAnim->setSize(mSize);
	}
	
	if (mRequest == nullptr && mWaitLoaded && mOnLoaded != nullptr && mLoadingTexture == nullptr && mTexture && mTexture->bind())
	{
		resize();
		mOnLoaded();
		mWaitLoaded = false;
	}

	ImageComponent::render(parentTrans);

	if (mRequest != nullptr && mBusyAnim != nullptr)
		mBusyAnim->render(trans);
}