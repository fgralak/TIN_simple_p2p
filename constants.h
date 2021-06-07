#pragma once

#include <string>

const int CHUNK_SIZE = 10000;
const int CLIENT_QUEUED_LIMIT = 20;
const int UPLOADER_COUNT = 3;
const int DOWNLOADER_COUNT = 3;
// const std::string TRACKER_IP = "127.0.0.1";
//const string TRACKER_PORT = "4500";
static constexpr unsigned MAX_SEND_SIZE = 1024;
const int DOWNLOAD_FAIL_LEVEL = 100000;

const int BUFF_SIZE = (1 << 15);

const char fileListNameSizeDelimiter = '/';

static constexpr double REFRESH_TIME_S {30.0};