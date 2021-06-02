#include <iostream>
#include <string>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>
#include <vector>
#include <array>
#include "helper_functions.h"
#include "torrent_parser.h"
#include "bencode_parser.h"

using namespace std;


struct params_t {
        pthread_mutex_t mutex;
        bool done = false;
        int intData;
        string stringData = string(" ");
};


const int CLIENT_QUEUED_LIMIT = 5;
const int UPLOADER_COUNT = 1;
const int DOWNLOADER_COUNT = 1;
const string TRACKER_IP = "127.0.0.1";
const string TRACKER_PORT = "4500";

int listenPort;

// Split string into parts
vector<string> split(string, char);

// Create .torrent from file
void createTorrentFile(string);

// Connect client to tracker
string connectWithTracker(string, string);

// Get list of available files from tracker
string getListFromTracker();

// Basic upload thread
void* uploadThread(void*);

// Basic download thread
void* downloadThread(void*);

// IO task
void* ioTask(void*);

// accepting task
void* acceptTask(void*);

int main(int argc, char* argv[])
{
    // Check if arguments are valid
    if(argc < 2)
    {
        printf("Usage: %s <Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    listenPort = atoi(argv[1]);

    // Create Listen Socket for upload threads to use
    int listenSocket = createTCPSocket();
    bindToPort(listenSocket, listenPort);

    if(listen(listenSocket, CLIENT_QUEUED_LIMIT) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // Create upload threads
    pthread_t uploadThreadID[UPLOADER_COUNT];
    
    // Create download threads
    pthread_t downloadThreadID[DOWNLOADER_COUNT];

    // Create IO thread
    pthread_t ioThread;
    params_t ioThreadParams;

    // Create accepter thread
    pthread_t accepterThread;
    params_t accepterThreadParams;


    array<params_t, UPLOADER_COUNT> uploadingParams;
    array<params_t, DOWNLOADER_COUNT> downloadingParams;
    unsigned currDownloaderCount = 0;
    unsigned currUploaderCount = 0;
    for (auto &uploader : uploadingParams)
    {
        pthread_mutex_init(&uploader.mutex, NULL);
    }
    for (auto &downloader : uploadingParams)
    {
        pthread_mutex_init(&downloader.mutex, NULL);
    }
    string action, args;
    
    pthread_mutex_init(&ioThreadParams.mutex, NULL);
    pthread_mutex_init(&accepterThreadParams.mutex, NULL);



    pthread_create(&ioThread, NULL, ioTask,
                   (void*) &ioThreadParams);

    accepterThreadParams.intData = listenSocket;
    pthread_create(&accepterThread, NULL, acceptTask,
                   (void*) &accepterThreadParams);


    while (true)
    {
        //check accepter
        int gotSocket = 0;
        if (accepterThreadParams.done)
        {
            pthread_join(accepterThread, NULL);

            if(accepterThreadParams.intData != listenSocket){

                uploadingParams[0] = accepterThreadParams;
                pthread_create(&uploadThreadID[0], NULL, uploadThread,
                                       (void*) &uploadingParams[0]);
            }

            gotSocket = accepterThreadParams.intData;
            accepterThreadParams.done = false;
            accepterThreadParams.intData = listenSocket;
            pthread_create(&accepterThread, NULL, acceptTask,
                           (void*) &accepterThreadParams);
        }



        //check IO
        if (ioThreadParams.done)
        {
            pthread_join(ioThread, NULL);
            args = ioThreadParams.stringData;
            ioThreadParams.done = false;
            pthread_create(&ioThread, NULL, ioTask,
                           (void*) &ioThreadParams);

            vector<string> argument = split(args, ' ');
            action = argument[0];

            if (action == "generate")
            {
                if (argument.size() < 2)
                {
                    printf("Invalid argument : <filename> \n");
                }
                else
                {
                    createTorrentFile(argument[1]);
                }
            }
            else if (action == "share")
            {
                if (argument.size() < 2)
                {
                    printf("Invalid argument : <filename.torrent>\n");
                }
                else
                {
                    string trackerResponse = connectWithTracker(argument[1],
                                                                "share");
                    printf("%s\n", trackerResponse.c_str());
//                    pthread_create(&uploadThreadID[0], NULL, uploadThread,
//                                   (void*) &listenSocket);
//    			pthread_join(uploadThreadID[0], NULL);
                }
            }
            else if (action == "get")
            {
                if (argument.size() < 2)
                {
                    printf("Invalid argument : <filename.torrent>\n");
                }
                else
                {

                    //TODO gdy nie zostanie utworzony watek do pobierania trzeba podzielic rzeczy do pobrania po rowno
                    string trackerResponse = connectWithTracker(argument[1],
                                                                "get");
                    printf("%s\n", trackerResponse.c_str());
                    vector<string> peers = split(trackerResponse, '$');
                    int numberOfPeers = peers.size() - 1;

                    for (int i = currDownloaderCount;
                            i < DOWNLOADER_COUNT && i < numberOfPeers
                            && currDownloaderCount < DOWNLOADER_COUNT; i++)
                    {
                        downloadingParams[i].stringData = peers[i];
                        pthread_create(&downloadThreadID[i], NULL,
                                       downloadThread,
                                       (void*) &downloadingParams[i]);
                        currDownloaderCount++;
                    }
                }
            }
            else if (action == "remove")
            {
                if (argument.size() < 2)
                {
                    printf("Invalid argument : <filename.torrent>\n");
                }
                else
                {
                    string trackerResponse = connectWithTracker(argument[1],
                                                                "remove");
                    printf("%s\n", trackerResponse.c_str());
                }
            }
            else if (action == "list")
            {
                string trackerResponse = getListFromTracker();
                printf("%s\n", trackerResponse.c_str());
            }
            else
            {
                printf("Unnkown command!\n");
            }
        }


        for (int i = 0; i < DOWNLOADER_COUNT; i++)
        {
            if (downloadingParams[i].done == true)
            {
                downloadingParams[i].done = false;
                pthread_join(downloadThreadID[i], NULL);
                currDownloaderCount--;
            }
        }
        for (int i = 0; i < UPLOADER_COUNT; i++)
        {
            if (uploadingParams[i].done == true)
            {
                uploadingParams[i].done = false;
                pthread_join(uploadThreadID[i], NULL);
                currUploaderCount--;
            }
        }
    }

    for (auto &downloader : downloadingParams)
    {
        pthread_mutex_destroy(&downloader.mutex);
    }
    for (auto &uploader : uploadingParams)
    {
        pthread_mutex_destroy(&uploader.mutex);
    }
    return 0;
}

vector<string> split(string txt, char ch)
{
    size_t pos = txt.find(ch);
    size_t initialPos = 0;
    vector<string> strs;
    
    while(pos != string::npos)
    {
        strs.push_back(txt.substr(initialPos, pos - initialPos));
        initialPos = pos + 1;
        pos = txt.find(ch, initialPos);
    }

    strs.push_back(txt.substr(initialPos, min(pos, txt.size()) - initialPos + 1));
    return strs;
}

void createTorrentFile(string name)
{
	string fs_name = name;
    ifstream in(fs_name.c_str(), ifstream::ate | ifstream::binary);
    int filesize = in.tellg(); 
    in.close();

    vector<string>T = split(name, '.');
    string initname = T[0];
    initname += ".torrent"; 
    FILE* output = fopen(initname.c_str(), "w");
    fprintf(output,"announceip %s\n", TRACKER_IP.c_str());
    fprintf(output,"announceport %s\n", TRACKER_PORT.c_str());
    fprintf(output,"filename %s\n", name.c_str());
    fprintf(output,"filesize %d\n", filesize);
    
    fprintf(stderr, "Torrent File Generated \n");
    fclose(output);
}

string connectWithTracker(string torrentfile, string msg)
{
    // Parse .torrent file
    TorrentParser torrentParser(torrentfile);

    // Server address
    sockaddr_in serverAddr;
    unsigned serverAddrLen = sizeof(serverAddr);
    memset(&serverAddr, 0, serverAddrLen);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(torrentParser.trackerIP.c_str());
    serverAddr.sin_port = htons(torrentParser.trackerPort);

    // Create a socket and connect to tracker
    int sockFD = createTCPSocket();	
    int connectRetVal = connect(sockFD, (sockaddr*) &serverAddr, serverAddrLen);
    if(connectRetVal < 0)
    {
        perror("connect() to tracker");
        closeSocket(sockFD);
        exit(EXIT_FAILURE);
    }

    printf("Connected to tracker\n");

    string trackerRequest = msg + "$";
    trackerRequest += "8:filename";
    trackerRequest += to_string(torrentParser.filename.size()) + ":" + torrentParser.filename;

    trackerRequest += "4:port";
    trackerRequest += "i" + to_string(listenPort) + "e";

    trackerRequest += "8:filesize";
    trackerRequest += "i" + to_string(torrentParser.filesize) + "e";

    int requestLen = trackerRequest.size();
    if(sendAll(sockFD, trackerRequest.c_str(), requestLen) == -1)
    {
        perror("sendall() failed");
        printf("Only sent %d bytes\n", requestLen);
    }

    char trackerResponse[BUFF_SIZE];
    memset(trackerResponse, 0, BUFF_SIZE);

    int responseLen = recv(sockFD, trackerResponse, BUFF_SIZE, 0);
    
    // Receive failed for some reason
    if(responseLen < 0)
    {
        perror("recv() failed");
        closeSocket(sockFD);
        return "";
    }

    // Connection closed by client
    if(responseLen == 0)
    {
        printf("Tracker closed connection without responding\n");
        closeSocket(sockFD);
        return "";
    }

    closeSocket(sockFD);
    return trackerResponse;
}

string getListFromTracker()
{
    // Server address
    sockaddr_in serverAddr;
    unsigned serverAddrLen = sizeof(serverAddr);
    memset(&serverAddr, 0, serverAddrLen);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(TRACKER_IP.c_str());
    serverAddr.sin_port = htons(stoi(TRACKER_PORT));

    // Create a socket and connect to tracker
    int sockFD = createTCPSocket();
    int connectRetVal = connect(sockFD, (sockaddr*) &serverAddr, serverAddrLen);
    if(connectRetVal < 0)
    {
        perror("connect() to tracker");
        closeSocket(sockFD);
        exit(EXIT_FAILURE);
    }

    printf("Connected to tracker\n");

    string trackerRequest = "list$";

    int requestLen = trackerRequest.size();
    if(sendAll(sockFD, trackerRequest.c_str(), requestLen) == -1)
    {
        perror("sendall() failed");
        printf("Only sent %d bytes\n", requestLen);
    }

    char trackerResponse[BUFF_SIZE];
    memset(trackerResponse, 0, BUFF_SIZE);

    int responseLen = recv(sockFD, trackerResponse, BUFF_SIZE, 0);
    
    // Receive failed for some reason
    if(responseLen < 0)
    {
        perror("recv() failed");
        closeSocket(sockFD);
        return "";
    }

    // Connection closed by client
    if(responseLen == 0)
    {
        printf("Tracker closed connection without responding\n");
        closeSocket(sockFD);
        return "";
    }

    closeSocket(sockFD);
    return trackerResponse;
}


void* uploadThread(void* arg) {
    printf("Uploader Thread %lu created\n", pthread_self());
    params_t* nArg = &(*(params_t*)(arg));
    int clientSocket = nArg->intData;
    string file = nArg->stringData;

    
    string peerData = "Sending a string.";

    int requestLen = peerData.size();
    if(sendAll(clientSocket, peerData.c_str(), requestLen) != 0)
    {
        perror("sendAll() failed");
        printf("Only sent %d bytes\n", requestLen);
    }

    closeSocket(clientSocket);


    return NULL;
}

void* downloadThread(void* arg)
{
    printf("Downloader Thread %lu created\n", pthread_self());
    params_t* nArg = &(*(params_t*)(arg));
    std::string trackerResponse = (static_cast <std::string> (nArg->stringData));
    printf("%s\n", trackerResponse.c_str());

    pthread_mutex_lock(&(*nArg).mutex);

    // Parse the tracker response
    BencodeParser bencodeParser(trackerResponse);

    // Peer address
    sockaddr_in peerAddr;
    unsigned peerAddrLen = sizeof(peerAddr);
    memset(&peerAddr, 0, peerAddrLen);
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_addr.s_addr = inet_addr(bencodeParser.peer_ip[0].c_str());
    peerAddr.sin_port = htons(bencodeParser.peer_port[0]);

    // Create a socket and connect to the peer
    int sockFD = createTCPSocket();
    int connectRetVal = connect(sockFD, (sockaddr*) &peerAddr, peerAddrLen);
    if(connectRetVal < 0)
    {
        perror("connect() to peer");
        closeSocket(sockFD);
        exit(EXIT_FAILURE);
    }

    // Send request to connected peer
    printf("Connected to peer %s:%d\n", bencodeParser.peer_ip[0].c_str(), bencodeParser.peer_port[0]);
    std::string peerRequest = "Check connection.";

    int requestLen = peerRequest.size();
    if(sendAll(sockFD, peerRequest.c_str(), requestLen) != 0)
    {
        perror("sendAll() failed");
        printf("Only sent %d bytes\n", requestLen);
    }

    // Get response from connected peer
    char peerResponse[BUFF_SIZE];
    memset(peerResponse, 0, BUFF_SIZE);

    int responseLen = recv(sockFD, peerResponse, BUFF_SIZE, 0);

    if(responseLen < 0)
    {
        perror("recv() failed");
        closeSocket(sockFD);
        return NULL;
    }

    // Connection closed by client
    if(responseLen == 0)
    {
        printf("Connection closed from client side\n");
        closeSocket(sockFD);
        return NULL;
    }

    printf("Downloaded data: %s\n", peerResponse);
    closeSocket(sockFD);

    pthread_mutex_unlock(&(*(params_t*)(arg)).mutex);
    (*(params_t*)(arg)).done = true;
    return NULL;
}

void* ioTask(void* arg)
{
    params_t *nArg = &(*(params_t*) (arg));
    printf("waiting for input: \n");
    pthread_mutex_lock(&(*nArg).mutex);

    string args;
    getline(cin, args);

    nArg->stringData = args; //actually it is not peer but what was read
    nArg->done = true;
    pthread_mutex_unlock(&(*nArg).mutex);
    pthread_exit(NULL);
}

void* acceptTask(void* arg)
{
    static constexpr unsigned bufSize = 1024;
    params_t *nArg = &(*(params_t*) (arg));
    int listenSocket = nArg->intData;
    pthread_mutex_lock(&(*nArg).mutex);

    sockaddr_in clientAddr;
    unsigned clientLen = sizeof(clientAddr);
    memset(&clientAddr, 0, clientLen);
    int clientSocket = accept(listenSocket,(sockaddr*) &clientAddr, (socklen_t*)&clientLen);


    char clientRequest[bufSize];
    memset(clientRequest, 0, bufSize);


    int requestMsgLen = recv(clientSocket, clientRequest, bufSize, 0);

    if(requestMsgLen < 0)
    {
        perror("recv() failed");
        closeSocket(clientSocket);
    }

    // Connection closed by client
    if(requestMsgLen == 0)
    {
        printf("Connection closed from client side\n");
        closeSocket(clientSocket);
    }

    nArg->stringData = string(clientRequest);
    nArg->intData = clientSocket;
    nArg->done = true;
    pthread_mutex_unlock(&(*nArg).mutex);
    pthread_exit(NULL);
}

