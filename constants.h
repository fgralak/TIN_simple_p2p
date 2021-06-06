#pragma once

#include <string>

const int CHUNK_SIZE = 100000;
const int CLIENT_QUEUED_LIMIT = 5;
const int UPLOADER_COUNT = 1;
const int DOWNLOADER_COUNT = 1;
const std::string TRACKER_IP = "127.0.0.1";
//const string TRACKER_PORT = "4500";
static constexpr unsigned MAX_SEND_SIZE = 1024;

const int BUFF_SIZE = (1 << 15);

const char fileListNameSizeDelimiter = '/';