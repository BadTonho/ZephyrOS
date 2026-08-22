#ifndef UPDATE_REMOTE_GITHUB_H
#define UPDATE_REMOTE_GITHUB_H

#include "types.h"
#include "core/update_remote.h"

#define UPDATE_REMOTE_GITHUB_RESPONSE_CAPACITY 65536U
#define UPDATE_REMOTE_GITHUB_PUBLISHED_SIZE 32U
#define UPDATE_REMOTE_GITHUB_RUNTIME_ASSET_MAX 16U

typedef struct {
    char name[UPDATE_REMOTE_PATH_SIZE];
    uint32_t size;
    char url[UPDATE_REMOTE_URL_SIZE];
    uint8_t digest_present;
    uint8_t digest[32];
} update_remote_github_asset_t;

typedef struct {
    char tag[UPDATE_REMOTE_TAG_SIZE];
    char release_id[UPDATE_REMOTE_RELEASE_ID_SIZE];
    char release_name[UPDATE_REMOTE_RELEASE_NAME_SIZE];
    char published_at[UPDATE_REMOTE_GITHUB_PUBLISHED_SIZE];
    update_remote_github_asset_t descriptor;
    update_remote_github_asset_t manifest;
    update_remote_github_asset_t package;
    update_remote_github_asset_t runtime_manifest;
    update_remote_github_asset_t runtime_package;
    update_remote_github_asset_t runtime_assets[
        UPDATE_REMOTE_GITHUB_RUNTIME_ASSET_MAX];
    uint16_t runtime_asset_count;
    uint8_t metadata_hash[32];
} update_remote_github_release_t;

int update_remote_github_query(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out);
int update_remote_github_runtime_query(
    const char* tag, const update_remote_options_t* options,
    update_remote_github_release_t* release_out,
    update_remote_result_t* result_out);

#endif
