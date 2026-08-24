#ifndef UPDATE_REMOTE_CONFIG_H
#define UPDATE_REMOTE_CONFIG_H

#define UPDATE_REMOTE_CONFIG_FORMAT "zephyros-update-remote-v2"
#define UPDATE_REMOTE_CHANNEL_NAME "stable"
#define UPDATE_REMOTE_DEFAULT_MANIFEST_URL \
    "http://10.0.2.2:8000/zephyros/stable.zum"
#define UPDATE_REMOTE_RELEASE_URL_TEMPLATE \
    "http://10.0.2.2:8000/zephyros/{tag}.json"

#define UPDATE_REMOTE_GITHUB_API_URL \
    "https://api.github.com"
#define UPDATE_REMOTE_GITHUB_OWNER "BadTonho"
#define UPDATE_REMOTE_GITHUB_REPOSITORY "ZephyrOS"
#define UPDATE_REMOTE_GITHUB_RELEASE_URL_TEMPLATE \
    "/repos/{owner}/{repo}/releases/tags/{tag}"
#define UPDATE_REMOTE_GITHUB_API_VERSION "2022-11-28"
#define UPDATE_REMOTE_GITHUB_DESCRIPTOR_NAME "release.json"
#define UPDATE_REMOTE_GITHUB_MANIFEST_NAME "release.zum"
#define UPDATE_REMOTE_GITHUB_PACKAGE_NAME "update.zephyrosupd"
#define UPDATE_REMOTE_GITHUB_SYSTEM_NAME "system.zsys"

#define UPDATE_REMOTE_RUNTIME_RELEASE_URL_TEMPLATE \
    "http://10.0.2.2:8000/zephyros/runtime-{tag}.json"
#define UPDATE_REMOTE_RUNTIME_MANIFEST_URL \
    "http://10.0.2.2:8000/zephyros/runtime.zum2"
#define UPDATE_REMOTE_RUNTIME_PACKAGE_NAME "runtime.zephyrosupd"
#define UPDATE_REMOTE_GITHUB_RUNTIME_MANIFEST_NAME "runtime.zum2"
#define UPDATE_REMOTE_GITHUB_RUNTIME_PACKAGE_NAME "runtime.zephyrosupd"

#endif
