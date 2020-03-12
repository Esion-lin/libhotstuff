#ifndef _IOTA_COO_H
#define _IOTA_COO_H
#include "IOTA_communication/Coo.h"

int Coo::listen_port;
int Coo::conn_fd;
Coo::Coo(deal_cb deal, int port){
	fun_deal = deal;
	listen_port = port;
	if(pthread_create(&coo_tid , NULL , init_listen, (void*)&fun_deal)== -1){
        HOTSTUFF_LOG_INFO("pthread create error.\n");
        exit(1);
    }
}
void* Coo::init_listen(void *deal){
	std::string host;
	int 		listen_fd;
	struct sockaddr_in 	server_sockaddr, client_addr;
	if((listen_fd = socket(AF_INET , SOCK_STREAM , 0)) == -1){
		perror("socket error.\n");
		exit(1);
	}
	server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(listen_port);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(listen_fd,(struct sockaddr *)&server_sockaddr,sizeof(server_sockaddr))==-1){
        perror("bind error.\n");
        exit(1);
    }
    if(listen(listen_fd,connect_pool) == -1){
        perror("listen error.\n");
        exit(1);
    }
    socklen_t length = sizeof(client_addr);
    char recvbuf[buffer_size*buffer_size];
    while(true){
        int conn = accept(listen_fd, (struct sockaddr*)&client_addr, &length);
        if(conn<0){
            perror("connect error.\n");
            exit(1);
        }else{
            //printf("connect successful\n");
        }    
        int ret = recv(conn, recvbuf, sizeof(recvbuf),0);
        if(ret <0){
            perror("recv error\n");
        }else{
            HOTSTUFF_LOG_INFO("recv size: %d\n",ret); 
            HOTSTUFF_LOG_INFO("recv data: %s\n",recvbuf); 
        }
        unsigned int id;
        uint8_t hash[256/8];
        if(byte_to_dic(id, hash, recvbuf, ret)){
        	HOTSTUFF_LOG_INFO("recv id: %u\n", id); 
        	deal_cb* deal_now = (deal_cb*) deal;
            (*deal_now)(id, hash);        
        }else{
            perror("reader error\n");
        } 
    }	
}

bool Coo::send_data(int port, uint8_t* data, int length){
	if((conn_fd = socket(AF_INET , SOCK_STREAM , 0)) == -1){
		perror("socket error.\n");
		exit(1);
		return false;
	}
	struct sockaddr_in 	Coo_server_sockaddr;
	Coo_server_sockaddr.sin_family = AF_INET;
    Coo_server_sockaddr.sin_port = htons(port); 
    Coo_server_sockaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    if(connect(conn_fd, (struct sockaddr *)&Coo_server_sockaddr, sizeof(Coo_server_sockaddr)) < 0){
        perror("connect");
        return false;
    }
    send(conn_fd, data, length,0);///send away
    close(conn_fd);
    return true;
}
#endif