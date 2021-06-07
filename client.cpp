#include <iostream>
#include <string>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>
#include <vector>
#include <array>
#include <unistd.h>
#include <limits>
#include <charconv>
#include <fstream>
#include <algorithm>
#include <queue>
#include <map>
#include "helper_functions.h"
#include "torrent_parser.h"
#include "bencode_parser.h"
#include "frame_definitions.h"
#include "constants.h"


using namespace std;




int listenPort;
std::string trackerPort;
std::string trackerIP = "";
string resourceDirectory = "";

// Create download threads
unsigned currDownloaderCount = 0;
pthread_t downloadThreadID[DOWNLOADER_COUNT];
array<params_t, DOWNLOADER_COUNT> downloadingParams;

// Create .torrent from file
void createTorrentFile(string);
void createTorrentFile(string name, int filesize);

// Connect client to tracker
string connectWithTracker(string, string);

// Get list of available files from tracker
string getListFromTracker();

// Basic upload thread
void* uploadThread(void*);

// Thread managing the download
void* downloadManagerThread(void*);

// Basic download thread
void* downloadThread(void*);

// IO task
void* ioTask(void*);

// accepting task
void* acceptTask(void*);

// Creates a connection with tracker, returns socket
int createConnection(string serverIp, int serverPort);

// Connect to tracker if connect==true; Disconnect from tracker otherwise
string switchConnectionToTracker(bool connect);

int main(int argc, char* argv[])
{
    srand(time(NULL));
    // Check if arguments are valid
    if(argc < 3)
    {
        printf("Usage: %s <Listen port> <Tracker port> <Tracker IP> <Resource directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if(argc > 4)
    {
        resourceDirectory = argv[4];
    }
    else
    {
        resourceDirectory = "";
    }
    if(argc > 3)
    {
        trackerIP = argv[3];
    }

    listenPort = atoi(argv[1]);
    trackerPort = argv[2];
    try
    {
        stoi(trackerPort);
    }
    catch(const std::exception& e)
    {
        cout<<"Invalid tracker port. Usage: "<<argv[0]<<" <Listen port> <Tracker port> <Tracker IP> <Resource directory>"<<endl;
        return 0;
    }
    

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

    // Create IO thread
    pthread_t ioThread;
    params_t ioThreadParams;

    // Create accepter thread
    pthread_t accepterThread;
    params_t accepterThreadParams;

    array<params_t, UPLOADER_COUNT> uploadingParams;
    
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

    switchConnectionToTracker(true);

    while (true)
    {
        //check accepter
        int gotSocket = 0;
        if (accepterThreadParams.done)
        {
            pthread_join(accepterThread, NULL);

            if(accepterThreadParams.intData != listenSocket){

                uploadingParams[currUploaderCount] = accepterThreadParams;
                pthread_create(&uploadThreadID[currUploaderCount], NULL, uploadThread,
                                       (void*) &uploadingParams[currUploaderCount]);
                currUploaderCount++;
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
                                                                to_string(ServerNodeCode::NodeNewFileAdded));
                    printf("%s\n", trackerResponse.c_str());
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
                                                                to_string(ServerNodeCode::NodeOwnerListRequest));
                    if (trackerResponse == "empty")
                    {
                        printf("file is not present in the network");

                    }
                    else
                    {
                        unsigned int threadId = getFirstFreeThread(
                            downloadThreadID, downloadingParams);
                        downloadingParams[threadId].stringData = 
                            trackerResponse;
                        downloadingParams[threadId].stringData1 = 
                            argument[1];
                        downloadingParams[threadId].threadId = 
                            threadId;
                        pthread_create(&downloadThreadID[threadId], 
                            NULL, downloadManagerThread, 
                            (void*) &downloadingParams[threadId]);
                        pthread_detach(downloadThreadID[threadId]);
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
                                                                to_string(ServerNodeCode::NodeFileDisclaim));
                    printf("%s\n", trackerResponse.c_str());
                }
            }
            else if (action == "connect")
            {
                string trackerResponse = switchConnectionToTracker(true);
                printf("%s\n", trackerResponse.c_str());
            }
            else if (action == "disconnect")
            {
                string trackerResponse = switchConnectionToTracker(false);
                printf("%s\n", trackerResponse.c_str());
            }
            else if (action == "list")
            {
                string trackerResponse = getListFromTracker();
                printf("%s\n", trackerResponse.c_str());
            }
            else
            {
                printf("Unknown command!\n");
            }
        }



        // join joinable
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

void createTorrentFile(string name)
{
    ifstream file;
    string resourceName = name;
    if(resourceDirectory != "")
    {
        resourceName = resourceDirectory+"/"+name;
    }
    file.open(resourceName, ios::in | ios::binary);
    file.ignore(numeric_limits<streamsize>::max());
    streamsize filesize = file.gcount();
    file.clear();   //  Since ignore will have set eof.
    file.seekg(0, std::ios_base::beg);
    file.close();

    createTorrentFile(name, filesize);
}

void createTorrentFile(string name, int filesize)
{
    vector<string>T = split(name, '.');
    string initname = T[0];
    initname += ".torrent";
    if(resourceDirectory != "")
    {
        initname = resourceDirectory+"/"+initname;
    }
    FILE* output = fopen(initname.c_str(), "w");
    fprintf(output,"announceip %s\n", trackerIP.c_str());
    fprintf(output,"announceport %s\n", trackerPort.c_str());
    fprintf(output,"filename %s\n", name.c_str());
    fprintf(output,"filesize %d\n", filesize);
    fclose(output);
}

int createConnection(string ip, int port)
{
    // Server address
    sockaddr_in serverAddr;
    unsigned serverAddrLen = sizeof(serverAddr);
    memset(&serverAddr, 0, serverAddrLen);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    serverAddr.sin_port = htons(port);

    // Create a socket and connect to tracker
    int sockFD = createTCPSocket();	
    int connectRetVal = connect(sockFD, (sockaddr*) &serverAddr, serverAddrLen);
    if(connectRetVal < 0)
    {
        string errorMsg = "connect() to "+ip+":"+to_string(port);
        perror(errorMsg.c_str());
        closeSocket(sockFD);
        exit(EXIT_FAILURE);
    }
    printf("Created connection with %s:%d\n", ip.c_str(), port);
    return sockFD;
}

string connectWithTracker(string torrentfile, string msg)
{
    TorrentParser torrentParser(torrentfile, resourceDirectory);
    int sockFD = createConnection(torrentParser.trackerIP, torrentParser.trackerPort);

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
    int sockFD = createConnection(trackerIP.c_str(), stoi(trackerPort));

    string trackerRequest = to_string(ServerNodeCode::NodeFileListRequest) + "$";

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
    
    if(string(trackerResponse) == "empty")
    {
        return "No available files\n";
    }
    string filenameListToPrint = "List of available files:\n";
    vector<string> listEntries = split(trackerResponse, '\n');
    vector<string> fileinfo;
    int filesize;
    for(auto &entry : listEntries)
    {
        if(entry.size() <= 1)
        {
            continue;
        }
        fileinfo = split(entry, fileListNameSizeDelimiter);
        if(fileinfo.size() != 2)
        {
            return "Could not read file info";
        }
        try
        {
            filesize = stoi(fileinfo[1]);
        }
        catch(const std::exception& e)
        {
            return "Error reading file size";
        }
        createTorrentFile(fileinfo[0], filesize);
        filenameListToPrint += fileinfo[0] + "\n";
    }

    return filenameListToPrint;
}


void* uploadThread(void* arg) {
    printf("Uploader Thread %lu created\n", pthread_self());
    params_t* nArg = &(*(params_t*)(arg));
    int clientSocket = nArg->intData;
    unsigned int chunkdId = nArg->chunkId;
    string fileStr = TorrentParser(nArg->stringData, resourceDirectory).filename;
    if(resourceDirectory != "")
    {
        fileStr = resourceDirectory+"/"+fileStr;
    }


    if( access( fileStr.c_str(), F_OK ) != 0 ) {
        // file doesnt exist
        string str = to_string(NodeNodeCode::NoSuchFile);
        int len =  str.length();
        if(sendAll(clientSocket, str.c_str(), len) != 0)
        {
            perror("file doesnt exist");
        }
    } else {
        // file exist
    }


    ifstream file;
    file.open(fileStr, ios::in | ios::binary);
    file.ignore(numeric_limits<streamsize>::max());
    streamsize length = file.gcount();
    file.clear();   //  Since ignore will have set eof.
    file.seekg(chunkdId * CHUNK_SIZE, std::ios_base::beg);

    char buffer[MAX_SEND_SIZE] = {0};

    int i = getChunkSize(length, chunkdId);
    // printf("Chunk id: %d, to read count: %d\n", chunkdId, i);
    while(i!=0)
    {
        int sendSize = min(i, (int)MAX_SEND_SIZE);
        if(!file.read(buffer, sendSize)) {printf("senderror");}
        int sl = send(clientSocket, buffer, sendSize, 0);
        // printf("Send %d bytes\n", sl);
        i -= sl;
    }
    file.close();
    closeSocket(clientSocket);
    printf("Upload of chunk %d done\n", chunkdId);
    return NULL;
}



void* downloadManagerThread(void* arg)
{
    params_t* nArg = &(*(params_t*)(arg));
    downloadingParams[nArg->threadId].isFree = false;
    vector<string> peers = split(nArg->stringData, '$');
    // for(unsigned int i = 0; i < peers.size() - 1; ++i)
    //     printf("Peer %d: %s\n", i, peers[i].c_str());
    int numberOfPeers = peers.size() - 1;

    std::string peerRequest = nArg->stringData1;
    auto size = TorrentParser(peerRequest, resourceDirectory).filesize;
    auto fName = TorrentParser(peerRequest, resourceDirectory).filename;
    auto resourceFilename = fName;
    if(resourceDirectory != "")
        resourceFilename = resourceDirectory+"/"+fName;

    ofstream file(resourceFilename);
    if(!file.is_open()) {
        printf("downloaded file creation error\n");
        exit(EXIT_FAILURE);
    }
    // printf("Resource filename: %s\n", resourceFilename.c_str());

    // get number of chunks
    unsigned int nChunks = size / CHUNK_SIZE;
    if(size % CHUNK_SIZE)
        ++nChunks;

    std::vector<bool> isDownloading(nChunks, false);
    std::vector<bool> isDownloaded(nChunks, false);
    std::queue<unsigned int> childThreadIds;
    std::map<unsigned int, unsigned int> childThreadIdToChunkIdMap;
    
    bool done = false;
    int iterationCounter = 0;
    // initial start of threads
    while(currDownloaderCount < DOWNLOADER_COUNT) {
        unsigned int chunkId = getFirstNotDownloaded(isDownloading);
        
        // find free thread
        unsigned int threadId = getFirstFreeThread(downloadThreadID, 
            downloadingParams);
                
        // printf("Initial thread start: Chunk id: %d\n", chunkId);
        if(chunkId == nChunks) {
            // printf("Breaking out of the loop\n");
            break;
        }
        childThreadIds.push(threadId);
        childThreadIdToChunkIdMap[threadId] = chunkId;
        // printf("Added chunk id %d to loop, thread no. %d\n", chunkId, 
        //     threadId);
        isDownloading[chunkId] = true;

        downloadingParams[threadId].stringData = 
            peers[random() % numberOfPeers];
        downloadingParams[threadId].stringData1 = nArg->stringData1;
        downloadingParams[threadId].chunkId = chunkId;
        pthread_create(&downloadThreadID[threadId], 
            NULL, downloadThread, 
            (void*) &downloadingParams[threadId]);
        currDownloaderCount++;
    }    

    while(!done) {
        // printf("Looping...\n");
        // wait for one thread
        if(!childThreadIds.empty()) {
            void* returnedValue;
            // printf("Joining %d\n", 
            //     childThreadIdToChunkIdMap[childThreadIds.front()]);
            pthread_join(downloadThreadID[childThreadIds.front()], 
                &returnedValue);
            --currDownloaderCount;
            unsigned int resultChunkId = 
                childThreadIdToChunkIdMap[childThreadIds.front()];
            childThreadIds.pop();

            if(!returnedValue) {
                // download not successful
                isDownloading[resultChunkId] = false;
                // printf("Download failed: %d\nDownloading: %d, downloaded: %d\n", 
                    // resultChunkId, getFirstNotDownloaded(isDownloading), 
                    // getFirstNotDownloaded(isDownloaded));
            }
            else {
                // update file
                isDownloaded[resultChunkId] = true;
                char* returnedString = (char*)returnedValue;
                file.seekp(resultChunkId * CHUNK_SIZE);
                file.write(returnedString, getChunkSize(size, resultChunkId));    
            //     printf("Download successful: %d\nDownloading: %d, downloaded: %d\n", 
            //         resultChunkId, getFirstNotDownloaded(isDownloading), 
            //         getFirstNotDownloaded(isDownloaded));
            }
        }
        
        
        // start another thread if necessary and possible
        unsigned int chunkId = getFirstNotDownloaded(isDownloading);
        if(chunkId != nChunks && currDownloaderCount < DOWNLOADER_COUNT) {
            unsigned int threadId = getFirstFreeThread(downloadThreadID, 
                downloadingParams);
            // printf("Latter thread start: Chunk id: %d\n", chunkId);
            childThreadIds.push(threadId);
            childThreadIdToChunkIdMap[threadId] = chunkId;
            // printf("Added chunk id %d to loop, thread no. %d\n", chunkId, 
                // threadId);
            isDownloading[chunkId] = true;

            downloadingParams[threadId].stringData = 
                peers[random() % numberOfPeers];
            downloadingParams[threadId].stringData1 = 
                nArg->stringData1;
            downloadingParams[threadId].chunkId = chunkId;

            pthread_create(&downloadThreadID[threadId], 
                NULL, downloadThread, 
                (void*) &downloadingParams[threadId]);
            currDownloaderCount++;
        }

        // check if whole download complete
        if(nChunks == getFirstNotDownloaded(isDownloaded))
            done = true;
        
        // workaround
        if(iterationCounter == DOWNLOAD_FAIL_LEVEL)
            break;
        ++iterationCounter;
    }

    file.close();

    printf("Downloaded file %s\n", fName.c_str());
    connectWithTracker(peerRequest, 
        to_string(ServerNodeCode::NodeFileDownloaded));

    --currDownloaderCount;
    downloadingParams[nArg->threadId].done = true;
    downloadingParams[nArg->threadId].isFree = true;
    return NULL;
}


void* downloadThread(void* arg)
{
    printf("Downloader Thread %lu created\n", pthread_self());
    params_t* nArg = &(*(params_t*)(arg));
    std::string trackerResponse = (static_cast <std::string> (nArg->stringData));
    // printf("%s\n", trackerResponse.c_str());

    //TODO this lock blocked the thread, even though it was the only one
    //pthread_mutex_lock(&(*nArg).mutex);

    // Parse the tracker response
    BencodeParser bencodeParser(trackerResponse);

    int sockFD = createConnection(bencodeParser.peer_ip[0], bencodeParser.peer_port[0]);

    // Send request to connected peer, specify chunk id
    printf("Connected to peer %s:%d\n", bencodeParser.peer_ip[0].c_str(), bencodeParser.peer_port[0]);
    std::string peerRequest = nArg->stringData1;
    std::string peerRequestWithChunkId = std::string("i") + 
        std::to_string(nArg->chunkId) + std::string("e") + nArg->stringData1;

    auto size = TorrentParser(peerRequest, resourceDirectory).filesize;
    auto fName = TorrentParser(peerRequest, resourceDirectory).filename;
    auto resourceFilename = fName;
    if(resourceDirectory != "")
    {
        resourceFilename = resourceDirectory+"/"+fName;
    }

    int requestLen = peerRequestWithChunkId.size();
    if(sendAll(sockFD, peerRequestWithChunkId.c_str(), requestLen) != 0)
    {
        perror("sendAll() failed");
        printf("Only sent %d bytes\n", requestLen);
    }

    char peerResponse[BUFF_SIZE] = {0};

    int i = getChunkSize(size, nArg->chunkId);
    int responseLen = 0;
    int toRecieve = min(size, (int)MAX_SEND_SIZE);
    responseLen = recv(sockFD, peerResponse, toRecieve, 0);
    i -= responseLen;
    string checkNoFile = string(peerResponse).substr(0, 3);
    
    // string to collect data
    string receivedData;

    if (checkNoFile == to_string(NodeNodeCode::NoSuchFile))
    {
        // handle no file
        return NULL;
    }
    else if (responseLen < 0)
    {
        perror("recv() failed");
        closeSocket(sockFD);
        return NULL;
    }
    // Connection closed by client
    else if(responseLen == 0)
    {
        printf("Connection closed from client side\n");
        closeSocket(sockFD);
        return NULL;
    }
    else
    {
        for(int j = 0; j < responseLen; ++j)
            receivedData += peerResponse[j];
        while (i > 0)
        {
            toRecieve = min(i, (int)MAX_SEND_SIZE);
            responseLen = recvfrom(sockFD, peerResponse, toRecieve, 0, NULL, NULL);
            for(int j = 0; j < responseLen; ++j)
                receivedData += peerResponse[j];
            i -= responseLen;
        }
    }


    closeSocket(sockFD);

    pthread_mutex_unlock(&(*(params_t*)(arg)).mutex);
    (*(params_t*)(arg)).done = true;
    (*(params_t*)(arg)).isFree = true;
    return (void*)receivedData.c_str();
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

    unsigned int chunkId = 0;
    int idToRead = 0;
    // ugly workaround to build chunk id
    if(clientRequest[0] == 'i') {
        idToRead = 1;
        while(clientRequest[idToRead] != 'e') {
            chunkId = chunkId * 10 + (clientRequest[idToRead] - '0');
            ++idToRead;
        }
        // move to first valid position
        ++idToRead;
    }

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

    nArg->stringData = string(clientRequest).substr(idToRead);
    nArg->intData = clientSocket;
    nArg->chunkId = chunkId;
    nArg->done = true;
    pthread_mutex_unlock(&(*nArg).mutex);
    pthread_exit(NULL);
}

string switchConnectionToTracker(bool connect)
{
    int sockFD = createConnection(trackerIP.c_str(), stoi(trackerPort));
    string trackerRequest;
    if(connect)
    {
        trackerRequest = to_string(ServerNodeCode::NodeConnect) + "$";
    }
    else
    {
        trackerRequest = to_string(ServerNodeCode::NodeDisconnect) + "$";
    }
    trackerRequest += "4:port";
    trackerRequest += "i" + to_string(listenPort) + "e";

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