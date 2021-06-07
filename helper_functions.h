#ifndef HELPER_FUNCTIONS_H
#define HELPER_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <fstream>
#include <array>
#include <string>
#include <filesystem>
#include "constants.h"
#include "torrent_parser.h"


struct params_t {
    pthread_mutex_t mutex;
    bool done = false;
    bool isFree = true;
    int intData;
    std::string stringData;
    std::string stringData1;
    unsigned int chunkId;
    unsigned int threadId;

    params_t()
    {
        stringData.reserve(1024);
        stringData1.reserve(1024);
    }
};



int createTCPSocket();
void bindToPort(int, short);
void closeSocket(int);

// Split string into parts
std::vector<std::string> split(std::string, char);

// Creates TCP Socket
int createTCPSocket()
{
    // Create TCP socket
    int sockFD = socket(AF_INET, SOCK_STREAM, 0);

    // Socket creation failed
    if(sockFD < 0)
    {
        perror("TCP socket creation");
        exit(EXIT_FAILURE);
    }

    // Socket creation successfull
    printf("Created socket %d\n", sockFD);
    return sockFD;
}

// Binds given socket to local port specified
void bindToPort(int sockFD, short port)
{
    sockaddr_in addrport;
    memset(&addrport, 0, sizeof(addrport));         // Reset

    addrport.sin_family = AF_INET;                  // IPv4
    addrport.sin_port = htons(port);                // Port number
    addrport.sin_addr.s_addr = htonl(INADDR_ANY);   // Binds to local IP

    // Bind to local port
    int status = bind(sockFD, (sockaddr*) &addrport, sizeof(addrport));
    if(status < 0)
    {
        perror("Binding to local port");
        closeSocket(sockFD);
        exit(EXIT_FAILURE);
    }

    printf("Bound socket %d to local port %d\n", sockFD, port);
}

int sendAll(int sockFD, const char* buff, int& len)
{
    int total = 0;
    int bytesLeft = len;
    int sendRetVal = 0;

    while(total < len)
    {
        sendRetVal = send(sockFD, buff + total, bytesLeft, 0);
        if(sendRetVal == -1)
            break;

        total += sendRetVal;
        bytesLeft -= sendRetVal;
    }

    len = total;
    return (sendRetVal == -1 ? -1 : 0);
}

void closeSocket(int sockFD)
{
    // Close socket
    printf("Closing socket %d...\n", sockFD);
    close(sockFD);
}

std::vector<std::string> split(std::string txt, char ch)
{
    size_t pos = txt.find(ch);
    size_t initialPos = 0;
    std::vector<std::string> strs;
    
    while(pos != std::string::npos) 
    {
        strs.push_back(txt.substr(initialPos, pos - initialPos));
        initialPos = pos + 1;
        pos = txt.find(ch, initialPos);
    }

    strs.push_back( txt.substr(initialPos, std::min(pos, txt.size()) - initialPos + 1));
    return strs;
}



unsigned int getFirstNotDownloaded(std::vector<bool> isDownloaded) {
    for(unsigned int i = 0; i < isDownloaded.size(); ++i)
        if(!isDownloaded[i])
            return i;
    return isDownloaded.size();
}

unsigned int getChunkSize(unsigned int totalSize, unsigned int chunkId) {
    unsigned int nChunks = totalSize / CHUNK_SIZE;
    if(totalSize % CHUNK_SIZE)
        ++nChunks;
    if(chunkId == nChunks - 1)
        return totalSize % CHUNK_SIZE;
    return CHUNK_SIZE;
}

unsigned int getFirstFreeThread(pthread_t* threadArray, 
    std::array<params_t, DOWNLOADER_COUNT>& threadParams) {
    
    int threadId = -1;
    for(int i = 0; i < DOWNLOADER_COUNT; ++i) {
        if(threadParams[i].isFree) {
            threadParams[i].isFree = false;
            return (unsigned int)i;
        }
    }
    
    perror("Could not find a free thread even though thread limit not met");
    exit(EXIT_FAILURE);
}

std::string getListOfFilesInDir(std::string dirPath)
{
    std::string ext(".torrent");
    std::string ret = "";
    for (auto &p : std::filesystem::directory_iterator(dirPath))
    {
        if (p.path().extension() == ext)
        {
            std::ifstream file;
            std::string filepath = dirPath + "/" + std::string(p.path().filename());
            file.open(filepath, std::ios::in | std::ios::binary);
            std::streamsize filesize = file.gcount();
            std::stringstream buffer;
            buffer << file.rdbuf();
            file.seekg(0, std::ios_base::beg);
            file.close();
            TorrentParser torrentParser(filepath);


        ret += torrentParser.filename;
        ret += "$";
        ret += std::to_string(torrentParser.filesize);
        ret += "$";
        }
    }
    return ret;
}

#endif
