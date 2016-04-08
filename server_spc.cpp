#include <iostream>
#include <string>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <cstring>
#include <errno.h>
#include <dirent.h>
#include <vector>
#include <set>
#include <algorithm>

#define MAXLINE 512
#define QLEN 5  // maximum connection queue length
#define BUFSIZE 4096
#define MSG_MAX 1024

using namespace std;

int passiveTCP(char* port);
void login(struct sockaddr_in cli_addr, int sock);
void init();
void print_welcome_message(int newfd);
void broadcast(char* buf);
bool increasing_function (int i,int j) { return (i<j); }   // for std::sort
void str_echo(int sockfd);
// int readline(int fd,char *user_vector[idx], int maxlen);
int server_function(int newsockfd);
int bin_to_dec(int n);
string convert_to_ip(int ip);


class CMD
{
	public:
		int argc;
		char** argv;
		int out_type;  // 0: no pipe; 
					   // 1: | 
					   // 2: ! 
					   // 3: >

		int line_shift; // 0: cmd | cmd
					    // else: cmd |line_shift
		string file_name;

		CMD(){
			argc = 1;
			out_type = 0;
			line_shift = 0;
		}
};

class PIPE
{
	public:
		int pi[2];
		bool in_use;
		int counter;
		bool gone;    // gone means not exist anymore
		bool open;
		bool to_in_use, from_in_use;
		PIPE()
		{
			pi[0] = -1;
			pi[1] = -1;
			in_use = false;
			counter = 0;
			gone = false;
			to_in_use = false;
			from_in_use = false;
			open = false;
		}
};

class USER
{
	public:
		bool is_alive;
		int user_id;
		string user_name;
		string user_ip;
		int user_port;
		int user_sockfd;
		
		int num_line;
		std::vector<PIPE> pipe_vector;
		bool *hop_record;
		PIPE pipes[1000];
		std::set<string> user_env_vars;

		bool first_time_enter;

		USER()
		{
			is_alive = false;
			user_id = -1;
			user_name = "(no name)";
			user_ip = "CGILAB";
			user_port = 511;

			user_sockfd = -1;
			num_line = -1;
			hop_record = new bool[1001];
			first_time_enter = true;
		}

		void logout()
		{
			char buf[MSG_MAX];
			sprintf(buf, "*** User '%s' left. ***\r\n", user_name.c_str());
			broadcast(buf);
			is_alive = false;
		}
};


std::set<string> env_vars;
vector<USER> user_vector;
set<string> occupied_name_string;
PIPE public_pipe[100];

extern int errno;

void sig_fork(int signo)
{
    pid_t pid;
    int stat;
    pid = waitpid(0, &stat, WNOHANG);
    
    return;
}

int main(int argc , char *argv[])
{
	int sockfd, msock, c, read_size, childpid;
	unsigned clilen;
	struct sockaddr_in serv_addr, cli_addr;
	char client_message[MSG_MAX];
	int i, maxi, client_set[FD_SETSIZE], nready, client_sockfd;
	
	// // vector<int>available_id_vector;
	// for(int j = 0; j<30; j++)
	// 	available_id_vector.push_back(j);

	char* port = argv[1];
	int port_num = atoi(port);
	int ssock;
	
	fd_set rfds;    // read file descriptor set
	fd_set afds;  	// active file descriptor set
	int alen; 		// from-address length
	int fd, nfds, fdmax;

	msock= passiveTCP(argv[1]);
	nfds = getdtablesize();

	int server_fd_record[3];
	for(int i=0; i<3; i++)
		server_fd_record[i] = dup(i);

	FD_ZERO(&afds);
	FD_SET(msock,&afds);

	occupied_name_string.insert("(no name)");

	while(1){

		memcpy(&rfds, &afds, sizeof(rfds));
		
		if(select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0) < 0)
		 	perror("select error");
	
		if (FD_ISSET(msock,&rfds)){
			

			clilen = sizeof(cli_addr);
			bzero((char *)&cli_addr,clilen);
			
			ssock = accept(msock, (struct sockaddr *)&cli_addr, &clilen);
			if(ssock < 0)	
				cout << "server: accept error" << endl;
			
			FD_SET(ssock,&afds);
			
			init();
			USER new_user;
			new_user.user_sockfd = ssock;

			user_vector.push_back(new_user);
			login(cli_addr,ssock);

			user_vector.back().user_env_vars = env_vars;
			env_vars.clear();
			if(occupied_name_string.find("(no name)") == occupied_name_string.end())
				occupied_name_string.insert("(no name)");
	

		}
		
		for(int fdi = 3; fdi < nfds; fdi++){
			
			if(FD_ISSET(fdi, &rfds) && fdi != msock){
				int idx_to_erase = 0;
				for(vector<USER>::iterator it=user_vector.begin(); it != user_vector.end(); ++it){

					if(it->user_sockfd == fdi){

						int return_value = server_function(it->user_sockfd);
						
						for(int j=0; j < 3; j++){

							close(j);
							dup(server_fd_record[j]);
						}
						if(return_value == 1){  // exit issued
						
							user_vector[idx_to_erase].logout();
							
							sleep(100);
							close(fdi);
							FD_CLR(fdi, &afds);
				

							// erase name
							set<string>::iterator it_name_to_erase = occupied_name_string.find(it->user_name);
							occupied_name_string.erase(it_name_to_erase); 
							
							user_vector.erase(user_vector.begin() + idx_to_erase);
						
						}
						break;
					}
					else
						idx_to_erase++;
				}
				}
		}
	}
	return 0;
}

int server_function(int newsockfd)
{
	//Reset the sinal proceesing method to default
	(void) signal(SIGCHLD,SIG_DFL);
	// close stdin stdout stderr in fd table
	// and set fd[0] fd[1] fd[2] to newsockfd

	int std_out = dup(1);

	
	for(int i=0; i<3; i++)
	{
		close(i);
		dup(newsockfd);
	}
	
	vector<USER>::iterator current_it;
	int index_of_current_user = -1;   // use this index to replace the current_user in user_vector with modified current_user 
	for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it){
		
		index_of_current_user++;
		if(it->user_sockfd == newsockfd)
		{
			current_it = it;
			break;		
		}
	}

	if(current_it->first_time_enter == true)
	{
		// init();
		// print_welcome_message();
		current_it->first_time_enter = false;
	}

	int num_line = current_it->num_line;    // need to be copied back to user_vector
	std::vector<PIPE> pipe_vector = current_it->pipe_vector;   // need to be copied back to user_vector
	int child_pid;
	int original_input = dup(0); 
	int original_output = dup(1);
	bool *hop_record = new bool[1001];    // need to be copied back to user_vector
	hop_record = current_it->hop_record;

	PIPE pipes[1000];   // need to be copied back to user_vector 
	memcpy(pipes,current_it->pipes,sizeof(current_it->pipes));

	for(int i=0;i<1001;i++)
		hop_record[i] = false;

	bool do_exit = false;
	int one_time_counter = 0;

	while( !do_exit )
	{
		if(one_time_counter == 1)
			break;
		one_time_counter++;

		string line;
		stringstream ss_line;
		stringstream ss_for_unknown;
		getline(cin,line);
		if(line[line.size()-1]=='\r')
			line = line.substr(0,line.size()-1);
		num_line++;
	
		num_line = num_line % 1000;

		ss_line << line;
		ss_for_unknown << line;
		int i_th_cmd = 0;

		string un_str;
		ss_for_unknown >> un_str;
		
		if(un_str == "exit")
			return 1; // break;
		// check if command is unknown by examing filename in environment variables
        DIR *dir;
        struct dirent *ent;
        string* cmd_list;
        bool is_known = false;
        int num_file = 0;
        bool is_to_public_pipe = false, is_from_public_pipe = false;   // >N and <N flag

        for(set<string>::iterator it = current_it->user_env_vars.begin(); it != current_it->user_env_vars.end(); ++it){
	        
	        if ((dir = opendir ((*it).c_str())) != NULL) {
	          	
	            while ((ent = readdir (dir)) != NULL) {
	                string filestr(ent->d_name);
	                if(un_str == filestr || un_str == "printenv" || un_str == "setenv" || un_str == "who" || un_str == "name" || un_str == "yell" || un_str == "tell"){
						is_known = true;
	            		break;
	            	}
	            }
	            closedir(dir);   
	        } else {
	 
	                cout << "Can't open directory" << endl;
	        }
	    }

        if(is_known)
       	{
       		for(vector<PIPE>::iterator it=pipe_vector.begin(); it != pipe_vector.end(); ++it){
       			if(it->counter > 0){
	       			hop_record[it->counter] = false;
	       			it->counter--;
	       			hop_record[it->counter] = true;
	       		}
	       		else if(it->counter == 0){
	       			hop_record[it->counter] = false;

	       		}
       		}
       	}
		
       	
		string cmd_str;

		while(ss_line >> cmd_str) {

			if(cmd_str == "who") {
				char buf[MSG_MAX];
				memset(buf, 0, MSG_MAX);
				sprintf(buf, "<ID>\t<nickname>\t<IP/port>\t<indicate me>\r\n");
				write(newsockfd,buf,strlen(buf));

				for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it) {
					cout << it->user_id <<  "       " << it->user_name << "       " << it->user_ip << "/" << it->user_port;
					if(it->user_sockfd == newsockfd)
						cout << "          <- me" << endl;
					else
						cout << endl;
				}
				write(newsockfd,"% ", 2);
				return 0;
				break;

			}
			else if(cmd_str == "yell"){

				string message_to_yell;
				getline(ss_line,message_to_yell);
				vector<USER>::iterator source_it;
				for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it){
					if(it->user_sockfd == newsockfd){
						source_it = it;
						break;
					}
				}
				for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it){
					// dup2(it->user_sockfd,1);
					// cout << "*** " << source_it->user_name << " yelled ***: " << message_to_yell << endl;
					char buf[MSG_MAX];
					memset(buf, 0, MSG_MAX);
					sprintf(buf, "*** %s yelled ***: %s\n", source_it->user_name, message_to_yell);
					write(it->user_sockfd,buf,strlen(buf));
				}
				write(newsockfd,"% ", 2);
				return 0;
				break;
			}
			else if(cmd_str == "tell"){

				int num_to_tell;
				bool user_exist = false;
				string message_to_tell;
				ss_line >> num_to_tell;
				getline(ss_line, message_to_tell);
				vector<USER>::iterator it_to_tell;
				for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it){
					if(it->user_id == num_to_tell){
						user_exist = true;
						it_to_tell = it;
						break;
					}
				}

				if(user_exist == false){
					// dup2(newsockfd,1);
					// cout << "*** Error: user #" << num_to_tell << " does not exist yet. *** " << endl;
					char buf[MSG_MAX];
					memset(buf, 0, MSG_MAX);
					sprintf(buf, "*** Error: user #%d does not exist yet. *** ",num_to_tell);
					write(newsockfd,buf,strlen(buf));	
				}
				else{
					dup2(it_to_tell->user_sockfd,1);
					vector<USER>::iterator source_it;
					for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it){
						if(it->user_sockfd == newsockfd){
							source_it = it;
							break;
						}
					}
					cout << "*** " << source_it->user_name << " told you ***: " << message_to_tell << endl;
				}	
				dup2(original_output,1);
				write(newsockfd,"% ", 2);
				return 0;
				break;
			}
			else if(cmd_str == "name"){
				string name_str;
				ss_line >> name_str;
				string ip_to_print;
				int port_to_print;
				string name_to_print;
				bool new_name_added = false;
				vector<USER>::iterator source_it;

				
				for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it) {

					if(it->user_sockfd == newsockfd){

						source_it = it;
						break;
					}
				}

				for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it) {
					
					if(it->user_sockfd == newsockfd){
						if(occupied_name_string.find(name_str) != occupied_name_string.end())  // name_str exists, prompt error message
						{
							dup2(it->user_sockfd,1);
							cout << "*** User '" << name_str << "' already exists. ***" << endl;
						}
						else{
							// set<string>::iterator old_name_it = occupied_name_string.find(it->user_name);
							// occupied_name_string.erase(old_name_it);
							
							string old_name = it->user_name;
						
							set<string>::iterator it_to_erase;
							it_to_erase = occupied_name_string.find(old_name);
							occupied_name_string.erase(it_to_erase);

							it->user_name = name_str;
							occupied_name_string.insert(name_str);

							for(vector<USER>::iterator it_usr = user_vector.begin(); it_usr != user_vector.end(); ++it_usr){
								if(occupied_name_string.find(it_usr->user_name) == occupied_name_string.end())
									occupied_name_string.insert(it_usr->user_name);
							}
							new_name_added = true;
						}
						
					}
				}

				if(new_name_added == true){
					for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it) {
						dup2(it->user_sockfd,1);
						cout << "*** User from " << source_it->user_ip << "/" << source_it->user_port << " is named '" << source_it->user_name << "'. ***" << endl;
					}
				}
				write(newsockfd,"% ", 2);
				return 0;
				break;
			}

			if(cmd_str == "exit") {
				do_exit = true;
				return 1; 
			}

			DIR *dir1;
        	struct dirent *ent1;
       		bool is_known1 = false;
       		for(set<string>::iterator it = current_it->user_env_vars.begin(); it != current_it->user_env_vars.end(); ++it){
		      
		        if (( dir1 = opendir((*it).c_str()) ) != NULL) {
		          	
		            while ((ent1 = readdir (dir1)) != NULL) {
		                string filestr(ent1->d_name); 
		                if(cmd_str == filestr || un_str == "printenv" || un_str == "setenv" || un_str == "who" || un_str == "name" || un_str == "yell" || un_str == "tell"){
							is_known1 = true;
		            		break;
		            	}
		            }
		            closedir(dir1);

		        } else {

		                cout << "Can't open directory" << endl;
		        }
		    }

	        if(is_known1 == false)
	        {
	        	PIPE temp;
        	
        	
        		temp.pi[0] = pipes[999].pi[0];
        		temp.pi[1] = pipes[999].pi[1];
        		temp.in_use = true;
		
 	        	for(int i=999; i > 0; i--){
	        	
        			pipes[(i)%1000].pi[0] = pipes[i-1].pi[0];
	        		pipes[(i)%1000].pi[1] = pipes[i-1].pi[1];
	        		pipes[(i)%1000].in_use = true;
	        		pipes[i-1].in_use = false;
	        		
	        		
	        	}

        		pipes[0].pi[0] = temp.pi[0];
        		pipes[0].pi[1] = temp.pi[1];
        		pipes[0].in_use = temp.in_use;
	        	
	        	cout << "Unknown command: [" << cmd_str << "]." << endl;
	        	break;
	        }

			stringstream ss_cmd;
			ss_cmd << cmd_str << "\n";
			i_th_cmd++;
			CMD cmd;
			bool is_append = false;
			int to_public_pipe_num;
			int from_public_pipe_num;
			while(ss_line >> cmd_str)
			{
				if(cmd_str == "|"){
					cmd.out_type = 1;
					cmd.line_shift = 0;
					break;
				}
				else if(cmd_str == "!"){
					cmd.out_type = 2;
					cmd.line_shift = 0;
					break;
				}
				else if(cmd_str == ">"){
					cmd.out_type = 3;
					ss_line >> cmd.file_name;
					is_append = false;
					break;
				}
				else if(cmd_str == ">>"){
					cmd.out_type = 3;
					ss_line >> cmd.file_name;
					is_append = true;
					break;
					
				}
				else if(cmd_str[0] == '|' || cmd_str[0] == '!'){
					stringstream eat_c;
					char c;
					eat_c << cmd_str;
					eat_c >> c >> cmd.line_shift;
					cmd.out_type = (cmd_str[0] == '|') ? 1 : 2;

				}
				else if(cmd_str[0] == '>'){
					stringstream eat_c;
					char c;
					eat_c << cmd_str;
					eat_c >> c;
					eat_c >> to_public_pipe_num;

					is_to_public_pipe = true;

					if(public_pipe[to_public_pipe_num].gone == true){
						dup2(newsockfd,1);
						cout << "*** Error: public pipe " << to_public_pipe_num << " does not exist yet. ***" << endl;
						write(newsockfd,"% ", 2);
						return 0;
						break;
					}
					else if(public_pipe[to_public_pipe_num].to_in_use == true){
						dup2(newsockfd,1);
						cout << "*** Error: public pipe " << to_public_pipe_num << " already exists. ***" << endl;
						write(newsockfd,"% ", 2);
						return 0;
						break;
					}
					else{     // public_pipe[public_pipe_num] is not occupied
						if(pipe(public_pipe[to_public_pipe_num].pi) < 0)
							cerr << "Can't create pipes.\n";
						else{  // pipe create successful
							public_pipe[to_public_pipe_num].to_in_use = true;
						}
					}

					// check whether >N follows reight after <N
					string cascade_public_str;
					ss_line >> cascade_public_str;

					if(cascade_public_str.size() > 1 && cascade_public_str[0] == '<'){
						is_from_public_pipe = true;
						stringstream catch_num;
						catch_num << cascade_public_str;
						char catch_num_c;
						catch_num >> catch_num_c;
						catch_num >> from_public_pipe_num;
						public_pipe[from_public_pipe_num].from_in_use = true;
						cmd.out_type = 6;   //  <N >M case (or >N <M)
					}
					else{
						// need to restore ss_line for further reading
						string remaining_str;
						getline(ss_line,remaining_str);
						remaining_str = cascade_public_str + " " + remaining_str;
						cmd.out_type = 4;
					}
					break;
				}
				else if(cmd_str[0] == '<'){
					stringstream eat_c;
					char c;
					eat_c << cmd_str;
					eat_c >> c;
					eat_c >> from_public_pipe_num;

					is_from_public_pipe = true;

				
					if(public_pipe[from_public_pipe_num].gone == true){
						dup2(newsockfd,1);
						cout << "*** Error: public pipe " << from_public_pipe_num << " does not exist yet. ***" << endl;
						write(newsockfd,"% ", 2);
						return 0;
						break;
					}
					else if(public_pipe[from_public_pipe_num].from_in_use == true){
						dup2(newsockfd,1);
						cout << "*** Error: public pipe " << from_public_pipe_num << " already exists. ***" << endl;
						write(newsockfd,"% ", 2);
						return 0;
						break;
					}
					else if(public_pipe[from_public_pipe_num].to_in_use == false){
						dup2(newsockfd,1);
						cout << "*** Error: public pipe " << from_public_pipe_num << " does not exist yet. ***" << endl;
						write(newsockfd,"% ", 2);
						return 0;
						break;
					}
					else{     // public_pipe[public_pipe_num] is not occupied
						if(pipe(public_pipe[from_public_pipe_num].pi) < 0)
							cerr << "Can't create pipes.\n";
						else{  // pipe create successful
							public_pipe[from_public_pipe_num].from_in_use = true;
							public_pipe[from_public_pipe_num].gone = true;
						}
					}
					dup2(original_output,1);
					
					// check whether >N follows reight after <N
					string cascade_public_str;
					ss_line >> cascade_public_str;

					if(cascade_public_str.size() > 1 && cascade_public_str[0] == '>'){
						is_to_public_pipe = true;
						stringstream catch_num;
						catch_num << cascade_public_str;
						char catch_num_c;
						catch_num >> catch_num_c;
						catch_num >> to_public_pipe_num;
						public_pipe[to_public_pipe_num].to_in_use = true;
						cmd.out_type = 6;   //  <N >M case (or >N <M)
					}
					else{
						// need to restore ss_line for further reading
						string remaining_str;
						getline(ss_line,remaining_str);
						remaining_str = cascade_public_str + " " + remaining_str;
						cmd.out_type = 5;
					}
					
					break;
				}
				else{
					ss_cmd << cmd_str << '\n';
					cmd.argc++;
				}
			}

			
			int from_file_fd;
			char from_file_path[21];
			if(cmd.out_type == 5 || cmd.out_type == 6){
				sprintf(from_file_path, "../public_pipe_%d.txt", from_public_pipe_num);
				from_file_fd = open(from_file_path, O_CREAT|O_WRONLY|O_CREAT, 0777);
				cmd.argc = cmd.argc + 1;
				ss_cmd << " " << from_file_path;
				int index = 0;
				for(vector<USER>::iterator it1 = user_vector.begin(); it1 != user_vector.end(); ++it1){
					if(it1->user_sockfd == newsockfd){
						break;
					}
					else
						index++;
				}
				char buffer[MSG_MAX];
				memset(buffer,0,MSG_MAX);
				sprintf(buffer,"*** %s (#%d) just received via '%s' ***\r\n", (user_vector[index].user_name).c_str(), user_vector[index].user_id, line.c_str());
				broadcast(buffer);
			}		
			int to_file_fd;
			char file_path[21];
			// mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
			
			if(cmd.out_type == 4 || cmd.out_type == 6){
				sprintf(file_path, "../public_pipe_%d.txt", to_public_pipe_num);
				
				int index = 0;
				for(vector<USER>::iterator it1 = user_vector.begin(); it1 != user_vector.end(); ++it1){
					if(it1->user_sockfd == newsockfd){
						break;
					}
					else
						index++;
				}

				to_file_fd = open(file_path, O_CREAT|O_WRONLY, 0777);
				char buffer[MSG_MAX];
				memset(buffer,0,MSG_MAX);
				sprintf(buffer,"*** %s (#%d) just piped '%s' ***\r\n", (user_vector[index].user_name).c_str(), user_vector[index].user_id, line.c_str());
				broadcast(buffer);
				// if(cmd.out_type == 6){
				// 	cmd.argc = cmd.argc + 1;
				// 	ss_cmd << " " << file_path;
				// }
					
				
			}

			cmd.argv = new char* [cmd.argc+1];
			for(int i=0; i < cmd.argc; i++)
			{
				string arg;
				ss_cmd >> arg;
				// if(arg != "printenv" && arg != "setenv" && i == 0 )
				// 	arg = "bin/" + arg;

				char* char_str = new char[arg.size()+1];
				strcpy(char_str,arg.c_str());
				string print(char_str);
				
				cmd.argv[i] = char_str;
			}
			cmd.argv[cmd.argc] = NULL;
			
			if( !strcmp(cmd.argv[0],"setenv") )
			{
				setenv(cmd.argv[1],cmd.argv[2],1);
				// update environment variable path (only valid for setting single variable)
				current_it->user_env_vars.clear();
				string env_str(cmd.argv[2]);
				current_it->user_env_vars.insert(env_str);   // only valid for setting single variable, string split need to be done if env_str = "bin;."
				//clear both stringstream
				ss_line.str("");
				ss_line.clear();
				ss_cmd.str("");
				ss_cmd.clear();
				write(newsockfd,"% ", 2);
				return 0;
				continue;
			}
			else if( !strcmp(cmd.argv[0],"printenv") )
			{
				dup2(std_out,1);
				
				dup2(original_output,1);
				for(int i=1; i < cmd.argc; i++)
				{
					// char* path = getenv(cmd.argv[i]);
					string path;
					for(set<string>::iterator it = current_it->user_env_vars.begin(); it != current_it->user_env_vars.end();){
						
						path = path + *it;
						++it;
						if(it != current_it->user_env_vars.end())
							path = path + ":";
						else
							break;
					}

					if(path != "")
						cout << cmd.argv[i] << "=" << path << endl;
					else
						cout << "Can't find " << cmd.argv[1] << endl;

				}
				ss_line.str("");
				ss_line.clear();
				ss_cmd.str("");
				ss_cmd.clear();
				write(newsockfd, "% ", 2);
				return 0;
				continue;
			}
			int file_fd;
			PIPE inter_cmd_pipe;
			PIPE new_inter_line_pipe;
			
			bool many_to_dest = false;

			
			if(cmd.out_type == 3)   //  >  case
			{  
				if(is_append == false){	
					if((file_fd = open(cmd.file_name.c_str(), O_CREAT|O_WRONLY, 0777)) < 0)
						cerr << "Can't open file: " << cmd.file_name << endl;
				}
				else{  // is_append is true => open file with append config
					if((file_fd = open(cmd.file_name.c_str(), O_APPEND|O_WRONLY, 0777)) < 0)
						cerr << "Can't open file: " << cmd.file_name << endl;
				}	
			}
			if(cmd.out_type != 0 && cmd.line_shift == 0) //  cmd1 | cmd2  (the condition with pipe(|, !, >))
			{
				if(pipe(inter_cmd_pipe.pi) < 0)
					cerr << "Can't create pipes." << endl;
				else
					inter_cmd_pipe.in_use = true;

			}
			else if(cmd.out_type != 0 && cmd.line_shift != 0) // cmd |N   or cmd !N (N is cmd.line_shift)
			{
				// check if there is pipe which has the same # of hops to go
				if(hop_record[cmd.line_shift] == true)
					many_to_dest = true; //there are same hops to go, no need to create new pipe
				else{
					// create new pipe
					if(pipe(new_inter_line_pipe.pi) < 0)
						cerr << "Can't create pipes.\n";
					else{
						new_inter_line_pipe.counter = cmd.line_shift;
						new_inter_line_pipe.in_use = true;
						hop_record[cmd.line_shift] = true;
						pipe_vector.push_back(new_inter_line_pipe);
						close(new_inter_line_pipe.pi[0]);
						close(new_inter_line_pipe.pi[1]);
					}
				}	
				if ( pipes[(num_line+cmd.line_shift)%1000].in_use == false )
				{
					many_to_dest = false;
					if ( pipe(pipes[(num_line+cmd.line_shift)%1000].pi) < 0)
						cerr << "Can't create pipes.\r\n";
					
					pipes[(num_line+cmd.line_shift)%1000].in_use = true;
				}
				else
					many_to_dest = true;	
					
					
			}
			
			// fork child process for exec
			if((child_pid = fork()) < 0)
				cerr << "Can't fork\r\n";
			else if(child_pid == 0)  // child
			{
				// set input
				close(0);
				
				if(cmd.out_type == 6){    //  >N <N case
					//dup(public_pipe[from_public_pipe_num].pi[0]);
					dup(from_file_fd);
					close(from_file_fd);
					public_pipe[from_public_pipe_num].from_in_use = true;
				}
				else if(cmd.out_type == 5){    //   <N case
					//dup(public_pipe[from_public_pipe_num].pi[0]);
					dup(from_file_fd);
					close(from_file_fd);
					
					public_pipe[from_public_pipe_num].from_in_use = true;
				}
				else if(cmd.out_type == 4){    //  >N
					//dup(inter_cmd_pipe.pi[0]);
					dup(original_input);
				}
				else if(pipes[num_line].in_use)
					dup(pipes[num_line].pi[0]);
				else
					dup(original_input);				
			
				// set output: write to pipeIn

				close(1);
				
				if(cmd.out_type == 6){    //  >N <N case
					//dup(public_pipe[to_public_pipe_num].pi[1]);
					dup(to_file_fd);
					close(to_file_fd);
					public_pipe[to_public_pipe_num].to_in_use = true;
				}
				else if(cmd.out_type == 5){   // <N
					dup(original_output);
				}
				else if(cmd.out_type == 4){   //  >N case
					// dup(public_pipe[to_public_pipe_num].pi[1]);
					dup(to_file_fd);
					close(to_file_fd);
					public_pipe[to_public_pipe_num].to_in_use = true;
				}	
				else if(cmd.out_type == 0){
					dup(original_output);
				}
				else if(cmd.out_type == 3){	// >
				
					dup(file_fd);
					close(file_fd);
				}
				else if(cmd.line_shift == 0 ){  // cmd1 | cmd2
											
					dup(inter_cmd_pipe.pi[1]);
					close(inter_cmd_pipe.pi[0]);
					close(inter_cmd_pipe.pi[1]);
				}
				else if ( cmd.line_shift != 0 && cmd.out_type == 1){		// cmd |N   (excluding cmd !N)

					dup(pipes[(cmd.line_shift+num_line)%1000].pi[1]);
				}
				else    //   cmd !N
					dup(original_output);
				
				// set error	
				close(2);
			
				if(cmd.out_type == 2){

						if(cmd.line_shift > 0)
							dup(pipes[(cmd.line_shift+num_line)%1000].pi[1]);
						else
							dup(1);
				}
				else
					dup(original_output);

				for(int i=0; i < 1000; i++){

					close(pipes[i].pi[0]);
					close(pipes[i].pi[1]);
				}

				// find where the command execution file is in
				DIR *dir;
        		struct dirent *ent;
        		bool is_known = false;
        		string cmd_name(cmd.argv[0]);
        		string path_to_append;
        		string concat_cmd_path;   // put it in the first argument of execvp function

        		for(set<string>::iterator it = current_it->user_env_vars.begin(); it != current_it->user_env_vars.end(); ++it){
	        
	        		bool dir_found = false;
		        	if (( dir = opendir((*it).c_str()) ) != NULL) {
		          	
		            	while ((ent = readdir (dir)) != NULL) {
		                	string filestr(ent->d_name);
		                	if(cmd_name == filestr){
								path_to_append = *it;
								dir_found = true;
								concat_cmd_path = path_to_append + "/" + filestr;
		            			break;
		            		}
		            	}
		            	closedir(dir);   
		        	} else {

		                cout << "Can't open directory" << endl;
		        	}
		        	if(dir_found == true)
		        		break;
	    		}

				// command execution
				int int_execvp;
				if ((int_execvp = execvp(concat_cmd_path.c_str(),cmd.argv)) < 0)
				{

					close(1);
					dup(original_output);

					cout << "Unknown command: [" << cmd.argv[0] << "]." << endl;

					close(0); 
					close(1);
					close(2);
					exit(-1);
				}
			}
			else   // parent
			{				
				close(pipes[num_line].pi[0]);
				close(pipes[num_line].pi[1]);
				pipes[num_line].in_use = false;

				if ( cmd.out_type == 3)	// >
					close(file_fd);	
				else if( cmd.out_type != 0 && cmd.line_shift == 0){  // cmd1 | cmd2 , cmd1 ! cmd2

						pipes[num_line].pi[0] = inter_cmd_pipe.pi[0];
						pipes[num_line].pi[1] = inter_cmd_pipe.pi[1];
						inter_cmd_pipe.in_use = true;
						pipes[num_line].in_use = true;

				}
				int status;

				while(waitpid(child_pid, &status, WNOHANG) != child_pid)  ;

				if(WIFEXITED(status) == false){

					close(pipes[num_line].pi[0]);
					close(pipes[num_line].pi[1]);
					pipes[num_line].in_use = false;

					if(cmd.out_type !=0 && cmd.line_shift != 0)
					{
						if(many_to_dest == false)  // only one previous command is piped to dest_line
						{
							close(pipes[(num_line+cmd.line_shift)%1000].pi[0]);
							close(pipes[(num_line+cmd.line_shift)%1000].pi[1]);
							pipes[(num_line+cmd.line_shift)%1000].in_use = false;
						}
					}
					ss_line.str("");
					ss_line.clear();

					write(newsockfd,"% ",2);
					return 0;
					continue;					
				}				
				if(cmd.out_type == 5)
					remove(from_file_path);
			}
		}
		if(do_exit)
			return 1;//break;
	}
	current_it->num_line = num_line;
	current_it->pipe_vector = pipe_vector;
	current_it->hop_record = hop_record;
	memcpy(current_it->pipes,pipes,sizeof(pipes));

	USER current_user;
	current_user.user_id = current_it->user_id;
	current_user.user_name = current_it->user_name;
	current_user.user_ip = current_it->user_ip;
	current_user.user_port = current_it->user_port;
	current_user.user_sockfd = current_it->user_sockfd;
	current_user.user_env_vars = current_it->user_env_vars;
	current_user.num_line = current_it->num_line;
	current_user.pipe_vector = current_it->pipe_vector;
	current_user.hop_record = current_it->hop_record;
	current_user.is_alive = current_it->is_alive;
	memcpy(current_user.pipes,current_it->pipes,sizeof(current_it->pipes));
	current_user.first_time_enter = current_it->first_time_enter;

	user_vector[index_of_current_user] = current_user;
	write(newsockfd, "% ", 2);
	return 0;
}

void init()
{
    setenv("PATH","bin:.",1);
    env_vars.insert("bin");
    env_vars.insert(".");
	chdir("ras");
}

int passiveTCP(char* port)
{
	int sockfd;
	struct sockaddr_in serv_addr;
	unsigned int servlen = sizeof(serv_addr);
	
	bzero((char *)&serv_addr, servlen);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons((u_short)atoi(port));

	// Open a TCP socket
	if ((sockfd = socket(AF_INET,SOCK_STREAM,0)) <0)
		cout << "server: can't open stream socket" << endl;
	//  Bind out locat address so that the client can send to us
	if (bind(sockfd,(struct sockaddr *)&serv_addr,servlen) <0)
		cout << "server: can't bind local address" << endl;
	
	listen(sockfd,QLEN);
	
	return sockfd;
}

void login(struct sockaddr_in cli_addr, int ssock)
{
	
	int idx = 0, id;

	for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it)
	{
		if (!it->is_alive)
		{
			
			user_vector[idx] = user_vector[idx];
		
			id = idx+1;
			break; 
		}
		idx++;
	}

	// get client source ip and socket
	socklen_t len;
	struct sockaddr_storage addr;
	char ipstr[INET6_ADDRSTRLEN];
	int port;

	len = sizeof addr;
	getpeername(ssock, (struct sockaddr*)&addr, &len);

	// deal with both IPv4 and IPv6:
	if (addr.ss_family == AF_INET) {
	    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
	    port = ntohs(s->sin_port);
	    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
	} else { // AF_INET6
	    struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
	    port = ntohs(s->sin6_port);
	    inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
	}

	string ip_readable(ipstr);
	user_vector[idx].user_ip = "CGILAB"; //ip_readable;
	user_vector[idx].user_port = 511;

	user_vector[idx].user_sockfd = ssock;
	user_vector[idx].user_id = id;

	print_welcome_message(ssock);
	
	user_vector[idx].is_alive = true;
	char buf[MSG_MAX];
	
	memset(buf, 0, MSG_MAX);
	
	sprintf(buf, "*** User '%s' entered from %s/%d. ***\r\n", (user_vector[idx].user_name).c_str(), (user_vector[idx].user_ip).c_str(), user_vector[idx].user_port);
	
	broadcast(buf);
	
	write(ssock, "% ",2);
}

void print_welcome_message(int newfd)
{
	
	char buf[MSG_MAX];
	memset(buf, 0, MSG_MAX);
	
	sprintf(buf,"****************************************\r\n");
	write(newfd, buf, strlen(buf));
	sprintf(buf,"** Welcome to the information server. **\r\n");
	write(newfd, buf, strlen(buf));
	sprintf(buf,"****************************************\r\n");
	write(newfd, buf, strlen(buf));
	
	vector<USER>::iterator usr_it;
	for(vector<USER>::iterator it=user_vector.begin(); it != user_vector.end(); ++it){
	
		if(it->user_sockfd == newfd){
			usr_it = it;
			break;
		}		
	}
}

void broadcast(char* buf)
{	
	
	for(vector<USER>::iterator it = user_vector.begin(); it != user_vector.end(); ++it){
			write(it->user_sockfd, buf, strlen(buf));
	}
}

int bin_to_dec(int n)
{
    int factor = 1;
    int total = 0;

    while (n != 0)
    {
        total += (n%10) * factor;
        n /= 10;
        factor *= 2;
    }

    return total;
}

string convert_to_ip(int ip)
{
    unsigned char bytes[4];
    char c[16];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;

    sprintf(c,"%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);        
    string rtn(c);
    return rtn;
}

