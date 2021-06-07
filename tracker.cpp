#include <iostream>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <atomic>
#include <map>
#include <set>
#include <vector>
#include "helper_functions.h"
#include "bencode_parser.h"
#include "frame_definitions.h"
#include "constants.h"

using namespace std;

// Type alias for vector of ip, port pairs
using ClientList = vector <pair<string, int>>;

// Set to true if termination of threads is needed
atomic <bool> terminateAllThreads;

// Filename -> Vector (IP, Port)
map < string, ClientList > mapping;

// Clients that are currently disconnected
ClientList clientsOffline;

// Filename -> File size
map <string, int > filesizeMap;

// Filename -> Owner IP, Owner port
map <string, pair<string, int>> fileOwner;

// Mutex for mapping
pthread_mutex_t mappingMutex;

// Function each thread executes
void* serverWorker(void*);

// Add the client details to mapping
string addToList(string, string, int, int filesize = 0);

// Remove the client details from mapping
string removeFromList(string, string, int);

// Get list of available files 
string getListOfFiles();

// Get list of clients having this file
ClientList getListOfClients(string);

// Refresh list of possible uploads
void refreshClientFiles();

string disconnectClient(string ip, int port);

string connectClient(string ip, int port);

bool isClientDisconnected(string ip, int port);
bool isAnyClientConnected(ClientList &clients);

int main(int argc, char* argv[])
{
    // Check if arguments are valid
    if(argc < 2)
    {
        printf("Usage: %s <Port Number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Get port number from command line
    short listenPort = atoi(argv[1]);
    // Number of clients which can queue up
    const int QUEUED_LIMIT = 10;
    // Number of server threads
    const int THREAD_COUNT = 4;

    // Create listening socket and set to listen
    int listeningSocketFD = createTCPSocket();
    bindToPort(listeningSocketFD, listenPort);
    if(listen(listeningSocketFD, QUEUED_LIMIT) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    terminateAllThreads = false;
    pthread_t threadID[THREAD_COUNT];
    for(int i = 0; i < THREAD_COUNT; i++)
        pthread_create(&threadID[i], NULL, serverWorker, (void*)&listeningSocketFD);

    static auto lastTime = chrono::steady_clock::now();
    while(!terminateAllThreads)
    {
        if (std::chrono::duration<double>(
                std::chrono::steady_clock::now() - lastTime).count()
            > REFRESH_TIME_S)
        {
            lastTime = std::chrono::steady_clock::now();
            refreshClientFiles();
        }
        else
        {
            sleep(5);
        }
    }

    for(int i = 0; i < THREAD_COUNT; i++)
        pthread_join(threadID[i], NULL);

    closeSocket(listeningSocketFD);
    return 0;
}

void* serverWorker(void *arg)
{
    printf("Server Thread %lu created\n", pthread_self());
    int listeningSocketFD = *(static_cast<int*>(arg));

    while (!terminateAllThreads)
    {
        sockaddr_in clientAddr;
        unsigned clientLen = sizeof(clientAddr);
        memset(&clientAddr, 0, clientLen);

        // Accept one client request
        int sockFD = accept(listeningSocketFD,
                            (sockaddr*) &clientAddr,
                            (socklen_t*) &clientLen);
        if (sockFD < 0)
        {
            perror("Accept failed");
            closeSocket(listeningSocketFD);
            printf("Terminating thread %lu\n", pthread_self());
            terminateAllThreads = true;
            pthread_exit (NULL);
        }

        char trackerRequest[BUFF_SIZE];
        memset(trackerRequest, 0, BUFF_SIZE);

        // Receive Tracker Request from client
        int requestMsgLen = recv(sockFD, trackerRequest, BUFF_SIZE, 0);

        // Receive failed for some reason
        if (requestMsgLen < 0)
        {
            perror("recv() failed");
            closeSocket(sockFD);
            continue;
        }

        // Connection closed by client
        if (requestMsgLen == 0)
        {
            printf("Connection closed from client side\n");
            closeSocket(sockFD);
            continue;
        }

        vector < string > request = split(trackerRequest, '$');
        if (request[0]
            == to_string(ServerNodeCode::NodeFileListRefreshRequest))
        {
            vector < string > files(request.begin() + 3, request.end());
            for (unsigned int i=0; i<files.size()-1; i+=2)
            {

                string ret = addToList(files[i],
                                       request[1],
                                       stoi(request[2]),
                                       stoi(files[i+1]));
            }

        }
        else
        {
            BencodeParser bencodeParser(request[1]);

            if (request[0] != to_string(ServerNodeCode::NodeFileListRequest))
            {
                bencodeParser.print_details();
            }

            string trackerResponse = "";

            if (request[0] == to_string(ServerNodeCode::NodeNewFileAdded))
            {
                auto element = mapping.find(bencodeParser.filename);
                if(element != mapping.end())
                {
                    trackerResponse = "File with the same name already exists in the network";
                }
                else
                {
                    trackerResponse = addToList(bencodeParser.filename,
                                            inet_ntoa(clientAddr.sin_addr),
                                            bencodeParser.port,
                                            bencodeParser.filesize);
                }
            }
            else if (request[0]
                    == to_string(ServerNodeCode::NodeOwnerListRequest))
            {
                ClientList clientList = getListOfClients(
                        bencodeParser.filename);
                for (auto client : clientList)
                {
                    trackerResponse += "6:peerip";
                    trackerResponse += to_string(client.first.size()) + ":";
                    trackerResponse += client.first;
                    trackerResponse += "8:peerport";
                    trackerResponse += "i" + to_string(client.second) + "e$";
                }
            }
            else if (request[0] == to_string(ServerNodeCode::NodeFileDisclaim))
            {
                trackerResponse = removeFromList(bencodeParser.filename,
                                                 inet_ntoa(clientAddr.sin_addr),
                                                 bencodeParser.port);
            }
            else if (request[0]
                    == to_string(ServerNodeCode::NodeFileListRequest))
            {
                trackerResponse = getListOfFiles();
            }
            else if (request[0]
                    == to_string(ServerNodeCode::NodeFileDownloaded))
            {
                addToList(bencodeParser.filename,
                          inet_ntoa(clientAddr.sin_addr), bencodeParser.port);
            }
            else if (request[0] == to_string(ServerNodeCode::NodeDisconnect))
            {
                trackerResponse = disconnectClient(
                        inet_ntoa(clientAddr.sin_addr), bencodeParser.port);
            }
            else if (request[0] == to_string(ServerNodeCode::NodeConnect))
            {
                trackerResponse = connectClient(inet_ntoa(clientAddr.sin_addr),
                                                bencodeParser.port);
            }

            if (trackerResponse == "")
                trackerResponse = "empty";

            int responseLen = trackerResponse.size();

            if (sendAll(sockFD, trackerResponse.c_str(), responseLen) != 0)
            {
                perror("sendall() failed");
                printf("Only sent %d bytes\n", responseLen);
            }

            // Serve client here
        }
        closeSocket(sockFD);
    }
    return NULL;
}

string addToList(string filename, string ip, int port, int filesize)
{
    // Add this client to the list
    pthread_mutex_lock(&mappingMutex);
    string response = "";
    if(isClientDisconnected(ip, port))
    {
        response = "Client is currently disconnected";
    }
    else
    {
        for(auto map_iter = mapping.begin(); map_iter != mapping.end() ; ++map_iter)
        {
            if(map_iter->first == filename)
            {
                for(auto vec_iter = map_iter->second.begin(); vec_iter != map_iter->second.end(); ++vec_iter)
                {
                    if(vec_iter->first == ip && vec_iter->second == port)
                    {
                        response = "Client is already on the list.";
                        break;
                    }
                }
                break;
            }
        }
        if(response == "")
        {
            if(filesize != 0)
            {
                filesizeMap[filename] = filesize;
            }
            mapping[filename].push_back({ip, port});
            if(mapping[filename].size() == 1)
            {
                fileOwner[filename] = make_pair(ip, port);
            }
            response = "Client details added to the list.";
        }
    }
    pthread_mutex_unlock(&mappingMutex);
    return response;
}

string removeFromList(string filename, string ip, int port)
{
    // Add this client to the list
    pthread_mutex_lock(&mappingMutex);
    string response = "Client is not on the list.";
    if(fileOwner[filename] == make_pair(ip, port))
    {
        mapping.erase(filename);
        filesizeMap.erase(filename);
        fileOwner.erase(filename);
        response = "File tracking stopped, file info removed";
    }
    else
    {
        for(auto map_iter = mapping.begin(); map_iter != mapping.end() ; ++map_iter)
        {
            if(map_iter->first == filename)
            {
                for(auto vec_iter = map_iter->second.begin(); vec_iter != map_iter->second.end(); ++vec_iter)
                {
                    if(vec_iter->first == ip && vec_iter->second == port)
                    {
                        map_iter->second.erase(vec_iter);
                        response = "Client details removed from the list.";
                        break;
                    }
                }
            }
            if(map_iter->second.size() == 0)
            {
            	mapping.erase(map_iter);
            	break;
            }
        }
    }
    pthread_mutex_unlock(&mappingMutex);
    return response;
}

string getListOfFiles()
{
	// Get list of files
    pthread_mutex_lock(&mappingMutex);
    string response = "";
    for(auto map_iter = mapping.begin(); map_iter != mapping.end() ; ++map_iter)
    {
        if(isAnyClientConnected(map_iter->second))
        {
            response += map_iter->first + fileListNameSizeDelimiter + to_string(filesizeMap[map_iter->first]) + "\n";
        }
    }
    pthread_mutex_unlock(&mappingMutex);
    return response;
}

ClientList getListOfClients(string filename)
{
    pthread_mutex_lock(&mappingMutex);
    ClientList retVal;
    for(auto &client : mapping[filename])
    {
        if(!isClientDisconnected(client.first, client.second))
        {
            retVal.push_back(client);
        }
    }
    pthread_mutex_unlock(&mappingMutex);
    return retVal;
}

string disconnectClient(string ip, int port)
{
    for(auto &client : clientsOffline)
    {
        if(client.first == ip && client.second == port)
        {
            return "Client already disconnected";
        }
    }
    clientsOffline.push_back(make_pair(ip, port));
    return "Client disconnected";
}

string connectClient(string ip, int port)
{
    ClientList::iterator clientIt;
    for(clientIt = clientsOffline.begin(); clientIt != clientsOffline.end(); ++clientIt)
    {
        if(clientIt->first == ip && clientIt->second == port)
        {
            break;
        }
    }
    if(clientIt != clientsOffline.end())
    {
        clientsOffline.erase(clientIt);
    }
    return "Client connected";
}

bool isClientDisconnected(string ip, int port)
{
    for(auto &client : clientsOffline)
    {
        if(client.first == ip && client.second == port)
        {
            return true;
        }
    }
    return false;
}

bool isAnyClientConnected(ClientList &clients)
{
    for(auto &client : clients)
    {
        if(!isClientDisconnected(client.first, client.second))
        {
            return true;
        }
    }
    return false;
}


void refreshClientFiles()
{
    pthread_mutex_lock(&mappingMutex);
    set <pair<string, int>> clientMap;



    for (auto &file : mapping)
    {
        for (auto &client : file.second)
        {
            clientMap.insert(client);
        }
    }

    for (auto& uniqueClient: clientMap)
    {
        sockaddr_in peerAddr;
        unsigned peerAddrLen = sizeof(peerAddr);
        memset(&peerAddr, 0, peerAddrLen);
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_addr.s_addr = inet_addr(uniqueClient.first.c_str());
        peerAddr.sin_port = htons(uniqueClient.second);

        int sockFD = createTCPSocket();
        int connectRetVal = connect(sockFD, (sockaddr*) &peerAddr, peerAddrLen);
        if(connectRetVal < 0)
        {
            perror("connect() to peer");
            closeSocket(sockFD);
            exit(EXIT_FAILURE);
        }

        string str = to_string(ServerNodeCode::ServerFileListRefreshRequest);
        auto strC = str.c_str();
        int len = str.length();
        if(sendAll(sockFD, strC, len) != 0)
        {
            perror("send in list refresh request failed");
        }
        closeSocket(sockFD);
    }
    mapping.clear();
    pthread_mutex_unlock(&mappingMutex);
}

