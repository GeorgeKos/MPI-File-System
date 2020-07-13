#include <mpi.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>

#include "data.h"

#define TAG 0
MPI_Datatype CUSTOM_ARRAY;

void coordinate(std::string filename, int num_servers, int np) {
    std::ifstream myfile;
    size_t pos = 0;
    std::string line;
    std::string delimiter = " ";
    std::vector<std::string> tokens;
    std::vector<int> server_ranks;
    std::set<int> available_client_ids;  // set of available ids for clients
    int temp, file_id, leader_id, server_rank, dummy_neighbour_ranks[2];
    myfile.open(filename);

    for (int i = 0; i < np; i++) {
        available_client_ids.insert(i);
    }
    while (getline(myfile, line)) {
        pos = 0;
        while ((pos = line.find(delimiter)) != std::string::npos) {  // split up the line into tokens
            tokens.emplace_back(line.substr(0, pos));
            line.erase(0, pos + delimiter.length());
        }
        tokens.emplace_back(line);

        // if we read a server
        if (tokens.at(0).compare("SERVER") == 0) {
            server_rank = stoi(tokens.at(1));               // save the server rank
            dummy_neighbour_ranks[0] = stoi(tokens.at(2));  // save left neighbor rank
            dummy_neighbour_ranks[1] = stoi(tokens.at(3));  // save right neighbor rank
            server_ranks.emplace_back(server_rank);         // all the server ids
            available_client_ids.erase(server_rank);        // erase server id from client ids
            MPI_Send(&dummy_neighbour_ranks, 1, CUSTOM_ARRAY, server_rank, (int)message_type::SERVER, MPI_COMM_WORLD);
            MPI_Recv(&temp, 1, MPI_INT, server_rank, (int)message_type::ACK, MPI_COMM_WORLD, 0);
        } else if (tokens.at(0).find("START_LEADER_ELECTION") != std::string::npos) {
            available_client_ids.erase(0);  //erasing id 0 from clients ( for some reason it was in the set???)

            for (int i = 0; i < num_servers; i++) {
                MPI_Send(&temp, 1, MPI_INT, server_ranks.at(i), (int)message_type::START_LEADER_ELECTION, MPI_COMM_WORLD);
            }
            MPI_Recv(&temp, 1, MPI_INT, MPI_ANY_SOURCE, (int)message_type::LEADER_ELECTION_DONE, MPI_COMM_WORLD, 0);
            leader_id = temp;
            std::cout << " LEADER ELECTION DONE\n";
            for (std::set<int>::iterator it = available_client_ids.begin(); it != available_client_ids.end(); ++it) {
                MPI_Send(&temp, 1, MPI_INT, *it, (int)message_type::CLIENT, MPI_COMM_WORLD);
            }
            for (int i = 0; i < available_client_ids.size(); i++) {
                MPI_Recv(&temp, 1, MPI_INT, MPI_ANY_SOURCE, (int)message_type::ACK, MPI_COMM_WORLD, 0);
            }
        } else if (tokens.at(0).compare("UPLOAD") == 0) {
            temp = stoi(tokens.at(1));  // client rank to send the UPLOAD message
            file_id = stoi(tokens.at(2));
            MPI_Send(&file_id, 1, MPI_INT, temp, (int)message_type::UPLOAD, MPI_COMM_WORLD);
        } else if (tokens.at(0).compare("UPDATE") == 0) {
            std::cout << " update \n";
        } else if (tokens.at(0).compare("RETRIEVE") == 0) {
            temp = stoi(tokens.at(1));  // client rank to send the RETRIEVE message
            file_id = stoi(tokens.at(2));
            MPI_Send(&file_id, 1, MPI_INT, temp, (int)message_type::RETRIEVE, MPI_COMM_WORLD);
        }
        tokens.clear();
    }
    for (std::set<int>::iterator it = available_client_ids.begin(); it != available_client_ids.end(); ++it) {  // SHUTDOWN to CLIENTS
        MPI_Send(&temp, 1, MPI_INT, *it, (int)message_type::REQUEST_SHUTDOWN, MPI_COMM_WORLD);
    }
    for (int i = 0; i < available_client_ids.size(); i++) {
        MPI_Recv(&temp, 1, MPI_INT, MPI_ANY_SOURCE, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD, 0);
    }
    std::cout << "SENDING SHUTDOWN TO LEADER: -" << leader_id << "- \n";  // SHUTDOWN to LEADER
    MPI_Send(&temp, 1, MPI_INT, leader_id, (int)message_type::REQUEST_SHUTDOWN, MPI_COMM_WORLD);
    MPI_Recv(&temp, 1, MPI_INT, MPI_ANY_SOURCE, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD, 0);
    myfile.close();
}

int main(int argc, char *argv[]) {
    int rank = 0, world_size, right_neighbour_rank = 0, left_neighbour_rank = 0, temp, active_requests = 0, times_received_shutdown = 0;
    bool candidate_id_sent = false;
    bool client_needs_toshutdown = false;
    bool connected_to_leader = false;
    int candidate_messages_received = 0;
    int client_source = 0;
    int num_servers = atoi(argv[1]);  // number of servers given from command line
    std::string testfile = argv[2];   // name of testfile
    /** MPI Initialisation **/
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Status status;
    MPI_Type_contiguous(3, MPI_INT, &CUSTOM_ARRAY);
    MPI_Type_commit(&CUSTOM_ARRAY);
    int temp_ints[3];
    std::unordered_map<int, int> saved_files;
    std::unordered_map<int, record> saved_records;
    std::vector<int> server_vector;
    std::vector<int> far_servers;
    std::vector<int> connected_servers;
    role myrole;
    int leader_id = rank;  // leader id starting with self rank
    if (rank == 0) {       // COORDINATOR
        std::cout << "[rank:" << rank << "] Coordinator started\n";
        myrole = role::COORDINATOR;
        coordinate(testfile, num_servers, world_size);
    } else {
        while (true) {
            MPI_Recv(&temp_ints, 1, CUSTOM_ARRAY, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            if (status.MPI_TAG == (int)message_type::SERVER) {  // UPON RECEIVING SERVER
                myrole = role::SERVER_PROCEDURE;
                left_neighbour_rank = temp_ints[0];
                right_neighbour_rank = temp_ints[1];
                MPI_Send(&temp, 1, MPI_INT, 0, (int)message_type::ACK, MPI_COMM_WORLD);  // sending ack back
                std::cout << "SERVER with rank: " << rank << " and left: " << left_neighbour_rank << ", right: " << right_neighbour_rank << std::endl;
            } else if (status.MPI_TAG == (int)message_type::START_LEADER_ELECTION) {  // LEADER ELECTION PHASE
                if (candidate_id_sent == false) {
                    temp_ints[0] = rank;
                    MPI_Send(&temp_ints[0], 1, MPI_INT, left_neighbour_rank, (int)message_type::CANDIDATE_ID, MPI_COMM_WORLD);  // sending CANDIDATE_ID to left neighbour
                    candidate_id_sent = true;
                }
            } else if (status.MPI_TAG == (int)message_type::CANDIDATE_ID) {  // UPON RECEIVING CANDIDATE_ID
                candidate_messages_received++;
                if (candidate_id_sent == false) {
                    temp_ints[0] = rank;
                    MPI_Send(&temp_ints[0], 1, MPI_INT, left_neighbour_rank, (int)message_type::CANDIDATE_ID, MPI_COMM_WORLD);  // sending CANDIDATE_ID to left neighbour
                    candidate_id_sent = true;
                }
                if (temp_ints[0] > leader_id) {
                    leader_id = temp_ints[0];
                }
                server_vector.emplace_back(temp_ints[0]);
                if (temp_ints[0] != rank && temp_ints[0] != left_neighbour_rank && temp_ints[0] != right_neighbour_rank) {  // making a vector with servers that exclude self and neighbours
                    far_servers.emplace_back(temp_ints[0]);
                }
                if (temp_ints[0] != rank) {
                    MPI_Send(&temp_ints[0], 1, MPI_INT, left_neighbour_rank, (int)message_type::CANDIDATE_ID, MPI_COMM_WORLD);
                }
                if (candidate_messages_received == num_servers && leader_id == rank) {                                        // I AM THE LEADER
                    server_vector.erase(std::remove(server_vector.begin(), server_vector.end(), rank), server_vector.end());  // removing myself from the server vector
                    int K = num_servers - 3;
                    for (int i = 0; i < K / 4; i++) {
                        int random_index = rand() % far_servers.size();
                        MPI_Send(&rank, 1, MPI_INT, far_servers.at(random_index), (int)message_type::CONNECT, MPI_COMM_WORLD);
                        MPI_Recv(&temp, 1, MPI_INT, MPI_ANY_SOURCE, (int)message_type::ACK, MPI_COMM_WORLD, 0);
                        connected_servers.emplace_back(temp);
                    }
                    MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::LEADER_ELECTION_DONE, MPI_COMM_WORLD);  // LEADER ELECTION DONE and sending my id to coordinator
                }
            } else if (status.MPI_TAG == (int)message_type::CONNECT) {  // UPON RECEIVING CONNECT
                leader_id = temp_ints[0];
                connected_to_leader = true;
                MPI_Send(&rank, 1, MPI_INT, leader_id, (int)message_type::ACK, MPI_COMM_WORLD);
                std::cout << "Server: " << rank << " CONNECTED TO: " << leader_id << std::endl;
            } else if (status.MPI_TAG == (int)message_type::CLIENT) {  // UPON RECEIVING CLIENT
                myrole = role::CLIENT_PROCEDURE;
                leader_id = temp_ints[0];
                std::cout << "[rank:" << rank << "] Client started with leader: " << leader_id << std::endl;
                MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::ACK, MPI_COMM_WORLD);
            } else if (status.MPI_TAG == (int)message_type::UPLOAD) {  // UPON RECEIVING UPLOAD
                if (myrole == role::SERVER_PROCEDURE) {
                    if (rank == leader_id) {  // UPLOAD FOR LEADER
                        client_source = status.MPI_SOURCE;
                        temp_ints[2] = 0;  // reseting client source
                        std::cout << "UPLOAD COMING FROM CLIENT:" << client_source << ",WITH FILE ID " << temp_ints[0] << std::endl;
                        if (saved_records.find(temp_ints[0]) == saved_records.end()) {
                            //std::cout << " we DID NOT find " << temp_ints[0] << std::endl;                             //file not found
                            temp_ints[2] = client_source;                                                               // the client source
                            record temp_record(temp_ints[0]);                                                           // temp record for file_id received from client
                            request temp_request(client_source, ((num_servers - 1) / 2) + 1, request_type::UPLOAD, 1);  // temp requestf with client ID, N/2 + 1 COUNT, UPLOAD type, VERSION 1
                            temp_record.request_queue->emplace(temp_request);                                           // place request in the queue
                            saved_records.insert(std::make_pair(temp_ints[0], temp_record));                            // save the record on hash table
                            int K = ((num_servers - 1) / 2) + 1;                                                        // random N/2 + 1 servers
                            for (int i = 0; i < K; i++) {
                                int random_index = rand() % server_vector.size();
                                temp_ints[1] = server_vector.at(random_index);
                                //std::cout << " SENDING UPLOAD FROM " << rank << ",TO ->" << temp_ints[1] << std::endl;                         // destination server to upload
                                if (std::find(connected_servers.begin(), connected_servers.end(), temp_ints[1]) != connected_servers.end()) {  // if we found it in the connected servers
                                    //std::cout << " SENDING UPLOAD FROM " << rank << ",TO ->" << random_index << std::endl;
                                    MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, temp_ints[1], (int)message_type::UPLOAD, MPI_COMM_WORLD);
                                } else {
                                    for (auto it = std::begin(server_vector); it != std::end(server_vector); ++it) {                          // start searching the server vector for a suitable edge
                                        if (std::find(connected_servers.begin(), connected_servers.end(), *it) != connected_servers.end()) {  // stop at the first connected server
                                            //std::cout << " SENDING UPLOAD FROM " << rank << ",TO ->" << *it << std::endl;
                                            MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, *it, (int)message_type::UPLOAD, MPI_COMM_WORLD);  // Send UPLOAD to the nearest
                                            break;
                                        } else {
                                            //std::cout << " SENDING UPLOAD FROM " << rank << ",TO ->" << *it << std::endl;
                                            MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, left_neighbour_rank, (int)message_type::UPLOAD, MPI_COMM_WORLD);
                                            break;
                                        }
                                    }
                                }
                            }
                        } else {                                                                                                   // we found the file in saved records
                            MPI_Send(&temp_ints[0], 1, MPI_INT, client_source, (int)message_type::UPLOAD_FAILED, MPI_COMM_WORLD);  // send back to client upload failed
                        }
                    } else {                         // UPLOAD FOR SERVER
                        if (rank == temp_ints[1]) {  // if UPLOAD was meant for me then save it
                            saved_files.insert(std::make_pair(temp_ints[0], 1));
                            if (connected_to_leader) {
                                MPI_Send(&temp_ints[0], 1, MPI_INT, leader_id, (int)message_type::UPLOAD_ACK, MPI_COMM_WORLD);  // if connected to leader send UPLOAD_ACK
                            } else {
                                MPI_Send(&temp_ints[0], 1, MPI_INT, left_neighbour_rank, (int)message_type::UPLOAD_ACK, MPI_COMM_WORLD);
                            }
                        } else {
                            if (left_neighbour_rank == leader_id) {  // sometimes we go full circle....wrong
                                std::cout << " Entered here from server...." << rank << " meant for " << temp_ints[1] << std::endl;
                            } else {
                                MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, left_neighbour_rank, (int)message_type::UPLOAD, MPI_COMM_WORLD);  // not meant for me i send it to the left
                            }
                        }
                    }
                } else if (myrole == role::CLIENT_PROCEDURE) {
                    saved_files.insert(std::make_pair(temp_ints[0], 1));  // pair with: file id: temp from message and initial version 1
                    active_requests++;
                    MPI_Send(&temp_ints[0], 1, MPI_INT, leader_id, (int)message_type::UPLOAD, MPI_COMM_WORLD);
                }
            } else if (status.MPI_TAG == (int)message_type::UPLOAD_FAILED) {  // UPON RECEIVING UPLOAD_FAILED
                if (myrole == role::CLIENT_PROCEDURE) {
                    active_requests--;
                    std::cout << "CLIENT " << rank << " FAILED TO UPLOAD " << temp_ints[0] << std::endl;
                    if (client_needs_toshutdown && active_requests <= 0) {
                        std::cout << "SENDING SHUTDOWN FROM CLIENT: -" << rank << "- \n";
                        MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD);
                        break;
                    }
                }
            } else if (status.MPI_TAG == (int)message_type::UPLOAD_OK) {  // UPON RECEIVING UPLOAD_FAILED
                active_requests--;
                std::cout << "CLIENT " << rank << " UPLOADED, FILE: " << temp_ints[0] << std::endl;
                if (client_needs_toshutdown && active_requests <= 0) {
                    std::cout << "SENDING SHUTDOWN FROM CLIENT: -" << rank << "- \n";
                    MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD);
                    break;
                }
            } else if (status.MPI_TAG == (int)message_type::UPLOAD_ACK) {  // LEADER - UPON RECEIVING UPLOAD_ACK
                //std::cout << "RECEIVED UPLOAD_ACK to ---" << rank << "-FROM-" << status.MPI_SOURCE << std::endl;
                if (rank == leader_id) {
                    std::unordered_map<int, record>::iterator it;
                    for (it = saved_records.begin(); it != saved_records.end(); it++) {
                        if (it->first == temp_ints[0]) {  // we found the file id in record hash table
                            it->second.front->count--;
                            if (it->second.front->count == 0) {
                                it->second.request_queue->pop();
                                MPI_Send(&temp_ints[0], 1, MPI_INT, it->second.front->id, (int)message_type::UPLOAD_OK, MPI_COMM_WORLD);
                            }
                            break;
                        }
                    }
                } else {
                    if (connected_to_leader) {
                        MPI_Send(&temp_ints[0], 1, MPI_INT, leader_id, (int)message_type::UPLOAD_ACK, MPI_COMM_WORLD);  // if connected to leader send UPLOAD_ACK
                    } else {
                        MPI_Send(&temp_ints[0], 1, MPI_INT, left_neighbour_rank, (int)message_type::UPLOAD_ACK, MPI_COMM_WORLD);
                    }
                }
            } else if (status.MPI_TAG == (int)message_type::RETRIEVE) {  // UPON RECEIVING RETRIEVE
                if (myrole == role::CLIENT_PROCEDURE) {
                    active_requests++;
                    MPI_Send(&temp_ints[0], 1, MPI_INT, leader_id, (int)message_type::RETRIEVE, MPI_COMM_WORLD);
                    //MPI_Recv(&temp, 1, MPI_INT, MPI_ANY_SOURCE, (int)message_type::RETRIEVE_OK, MPI_COMM_WORLD, 0);
                } else if (myrole == role::SERVER_PROCEDURE) {
                    if (rank == leader_id) {  // RETRIEVE FOR LEADER
                        client_source = status.MPI_SOURCE;
                        std::cout << "RETRIEVE COMING FROM CLIENT:" << client_source << ",WITH FILE ID " << temp_ints[0] << std::endl;
                        std::unordered_map<int, record>::const_iterator it = saved_records.find(temp_ints[0]);
                        if (it == saved_records.end()) {                                                                             //file not found
                            MPI_Send(&temp_ints[0], 1, MPI_INT, client_source, (int)message_type::RETRIEVE_FAILED, MPI_COMM_WORLD);  // send back to client retrieve failed
                        } else {                                                                                                     // we found the file in saved records
                            request temp_request(client_source, ((num_servers - 1) / 2) + 1, request_type::RETRIEVE, 1);             // temp requestf with client ID, N/2 + 1 COUNT, RETRIEVE type, VERSION 1
                            it->second.request_queue->emplace(temp_request);                                                         // place request in the queue
                            int K = ((num_servers - 1) / 2) + 1;                                                                     // random N/2 + 1 servers
                            for (int i = 0; i < K; i++) {
                                int random_index = rand() % server_vector.size();
                                temp_ints[1] = server_vector.at(random_index);                                                                 // destination server to RETRIEVE
                                if (std::find(connected_servers.begin(), connected_servers.end(), temp_ints[1]) != connected_servers.end()) {  // if we found it in the connected servers
                                    MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, temp_ints[1], (int)message_type::RETRIEVE, MPI_COMM_WORLD);
                                } else {
                                    for (auto it = std::begin(server_vector); it != std::end(server_vector); ++it) {                          // start searching the server vector for a suitable edge
                                        if (std::find(connected_servers.begin(), connected_servers.end(), *it) != connected_servers.end()) {  // stop at the first connected server
                                            MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, *it, (int)message_type::RETRIEVE, MPI_COMM_WORLD);          // Send RETRIEVE to the nearest
                                            break;
                                        } else {
                                            MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, left_neighbour_rank, (int)message_type::RETRIEVE, MPI_COMM_WORLD);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    } else {                         // RETRIEVE FOR SERVER
                        if (rank == temp_ints[1]) {  // if RETRIEVE was meant for me then search it
                            std::unordered_map<int, int>::const_iterator it = saved_files.find(temp_ints[0]);
                            if (it == saved_files.end()) {  //not found
                                temp_ints[2] = 0;
                            } else {
                                temp_ints[2] = it->second;  // save version to send
                            }
                            if (connected_to_leader) {
                                MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, leader_id, (int)message_type::RETRIEVE_ACK, MPI_COMM_WORLD);  // if connected to leader send RETRIEVE_ACK
                            } else {
                                MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, left_neighbour_rank, (int)message_type::RETRIEVE_ACK, MPI_COMM_WORLD);
                            }
                        } else {                                     // not meant for me i send it to the left
                            if (left_neighbour_rank == leader_id) {  // sometimes we go full circle....wrong
                                std::cout << " (retrieve) Entered here from server...." << rank << " meant for " << temp_ints[1] << std::endl;
                            } else {
                                MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, left_neighbour_rank, (int)message_type::RETRIEVE, MPI_COMM_WORLD);
                            }
                        }
                    }
                }
            } else if (status.MPI_TAG == (int)message_type::RETRIEVE_ACK) {
                if (rank == leader_id) {
                    std::unordered_map<int, record>::iterator it;
                    for (it = saved_records.begin(); it != saved_records.end(); it++) {
                        if (it->first == temp_ints[0]) {               // we found the file id in record hash table
                            it->second.front->version = temp_ints[2];  //saving the new version
                            it->second.front->count--;
                            if (it->second.front->count == 0) {
                                it->second.request_queue->pop();
                                MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, it->second.front->id, (int)message_type::RETRIEVE_OK, MPI_COMM_WORLD);
                            }
                            break;
                        }
                    }
                } else {
                    if (connected_to_leader) {
                        MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, leader_id, (int)message_type::UPLOAD_ACK, MPI_COMM_WORLD);  // if connected to leader send UPLOAD_ACK
                    } else {
                        MPI_Send(&temp_ints, 1, CUSTOM_ARRAY, left_neighbour_rank, (int)message_type::UPLOAD_ACK, MPI_COMM_WORLD);
                    }
                }
            } else if (status.MPI_TAG == (int)message_type::RETRIEVE_OK) {  // UPON RECEIVING RETRIEVE_OK
                active_requests--;
                std::cout << "CLIENT " << rank << " RETRIEVED VERSION: " << temp_ints[2] << " OF " << temp_ints[0] << std::endl;
                if (client_needs_toshutdown && active_requests <= 0) {
                    std::cout << "SENDING SHUTDOWN FROM CLIENT: -" << rank << "- \n";
                    MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD);
                    break;
                }
            } else if (status.MPI_TAG == (int)message_type::RETRIEVE_FAILED) {  // UPON RECEIVING RETRIEVE_FAILED
                if (myrole == role::CLIENT_PROCEDURE) {
                    active_requests--;
                    std::cout << "CLIENT " << rank << " FAILED TO RETRIEVE " << temp_ints[0] << std::endl;
                    if (client_needs_toshutdown && active_requests <= 0) {
                        std::cout << "SENDING SHUTDOWN FROM CLIENT: -" << rank << "- \n";
                        MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD);
                        break;
                    }
                }
            } else if (status.MPI_TAG == (int)message_type::REQUEST_SHUTDOWN) {
                if (myrole == role::CLIENT_PROCEDURE) {
                    if (active_requests == 0) {
                        std::cout << "SHUTDOWN_OK CLIENT: -" << rank << "- \n";
                        MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD);
                        break;
                    } else {
                        client_needs_toshutdown = true;
                    }
                } else if (myrole == role::SERVER_PROCEDURE) {
                    if (rank == leader_id) {
                        times_received_shutdown++;
                        if (times_received_shutdown > 1) {
                            MPI_Send(&rank, 1, MPI_INT, 0, (int)message_type::SHUTDOWN_OK, MPI_COMM_WORLD);
                            break;
                        } else {
                            std::cout << "SENDING SHUTDOWN TO SERVER: -" << left_neighbour_rank << "- \n";
                            MPI_Send(&rank, 1, MPI_INT, left_neighbour_rank, (int)message_type::REQUEST_SHUTDOWN, MPI_COMM_WORLD);
                        }
                    } else {
                        std::cout << "SENDING SHUTDOWN TO SERVER: -" << left_neighbour_rank << "- \n";
                        MPI_Send(&rank, 1, MPI_INT, left_neighbour_rank, (int)message_type::REQUEST_SHUTDOWN, MPI_COMM_WORLD);  // send shutdown_ok to left neighbour and exit
                        break;
                    }
                }
            }
        }
    }
    MPI_Finalize();
}