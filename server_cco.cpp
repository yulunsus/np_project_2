#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <errno.h>
#include <set>
#include <dirent.h>

#define QLEN 30
#define MSG_MAX 1024
#define SHMSIZE 10240
#define SHMSERV (key_t) 10070
#define LOCKSERV (key_t) 10170
#define LOCKSTART (key_t) 10270

using namespace std;


void init();
int server_function(int idx);
void err_dump(const char* msg);
int passiveTCP(char* port);
void broadcast(int idx,char* msg);
int login( struct sockaddr_in cli_addr, int sock);
void reaper(int sig);
void write_SHMSERV();
void read_SHMSERV();
void read_public_channel(int sig);
void write_public_channel(int id, char* msg);
void P(int s);
void V(int s);
void create_LOCKSHM();
void delete_LOCKSHM(int sig);

class PIPE
{
public:
	int ps[2];	 // pipes
	bool is_alive;
	
	PIPE()
	{
		ps[0] = ps[1] = -1;
		is_alive = false;
	}
	void clear()
	{
		if (is_alive)
		{
			close(ps[0]);
			close(ps[1]);
			is_alive = false;
		}
	}
	void open()
	{
		if ( pipe(ps) < 0 )
			err_dump("can't create pipes.\r\n");
		else
			is_alive = true;
	}
};

class CMD
{
public:
	int argc;
	char** argv;
	int out_type;	// 0: no pipe 
					// 1: |
				 	// 2: !
				 	// 3: >
					// 4: >N
				 	
	int line_shift;	// 0  : cmd1 | cmd2
					// else : cmd |N  or cmd >N
	int in_from;
	string file_name; 
	

	CMD()
	{
		argc = 1;
		out_type = 0;
		line_shift = 0;
		in_from = -1;
	}
};

class USER
{
	public:
		int user_pid;
		int user_sockfd;
		int user_id;
		char user_name[20];
		string user_ip;
		int user_port;
		int num_line;

		PIPE pipes[1000];
		bool is_alive;
		set<string> env_vars;
		USER()
		{
			is_alive = false;
		}

		void user_init( struct sockaddr_in cli_addr, int sock, int uid)
		{
			user_sockfd = sock;
			user_id = uid;
			num_line = -1;
			strcpy(user_name,"(no name)");
			user_ip = "CGILAB";
			user_port = 511;
			for(int i=0; i<1000; i++)
				pipes[i].clear();
			env_vars.insert(".");
			env_vars.insert("bin");
		}
		void print_welcome_message()
		{
			char buffer[MSG_MAX];
			memset(buffer,0,MSG_MAX);
			sprintf(buffer,"****************************************\r\n");
			write(user_sockfd,buffer,strlen(buffer));
			sprintf(buffer,"** Welcome to the information server. **\r\n");
			write(user_sockfd,buffer,strlen(buffer));
			sprintf(buffer,"****************************************\r\n");
			write(user_sockfd,buffer,strlen(buffer));
		}

		void on_exit()
		{
			for(int i=0; i<1000; i++)
				pipes[i].clear();

			// chdir("..");
			// int mask = umask(0);
			// for(int i=0; i < 100; i++)
			// {
			// 	char fifo_name[11];
			// 	int fifo;
			// 	memset(fifo_name, 0, 11);
			// 	sprintf(fifo_name, "fifo%d", user_id, i+1);

			// 	umask(0);
			// 	if((fifo = open(fifo_name,O_RDONLY|O_NONBLOCK)) < 0)
			// 		;
			// 	else{			
					
			// 		close(fifo);
			// 		remove(fifo_name);
			// 	}
				
			// 	memset(fifo_name,0,11);
			// 	sprintf(fifo_name, "fifo%d", i+1, user_id);
			
			// 	umask(0);
			// 	if((fifo = open(fifo_name,O_RDONLY|O_NONBLOCK)) < 0);
			// 	else{

			// 		close(fifo);
			// 		remove(fifo_name);
			// 	}
			// }
			// umask(mask);
			chdir("ras");
			is_alive = false;
		}
};

class PUBLIC_PIPE
{
	public:
		bool is_open;
		bool is_gone;
		int public_pipe_id;
	
		PUBLIC_PIPE()
		{
			is_open = false;
			is_gone = false;
			public_pipe_id = -1;
		}

};

USER user[30];
PUBLIC_PIPE public_pipe[100];
int public_pipe_counter = 0;
int opened_public_file;
fd_set afds;
key_t MYSHM;
key_t MYLOCK;
int MYFD;
int server_pid;

int main(int argc, char* argv[])
{
	char buffer[MSG_MAX];
	int msock,ssock;
	struct sockaddr_in cli_addr;
	unsigned int clilen;
	int nfds;
	int original_output = dup(1);

	fd_set rfds;

	char filename[22];
	sprintf(filename, "opened_public_file.txt");
	opened_public_file = open(filename, O_CREAT|O_RDWR, 0777);
	close(opened_public_file);

	std::ofstream ofs;
	ofs.open("opened_public_file.txt", std::ofstream::out | std::ofstream::trunc);
	ofs << "";
	ofs.close();

	

	if(argc != 2)
		err_dump("Usage: server_select [port]\n");

	server_pid = getpid();
	
	init();

	msock = passiveTCP(argv[1]);

	nfds = getdtablesize();

	FD_ZERO(&afds);
	FD_SET(msock,&afds);		// put msock into afds

	create_LOCKSHM();

	(void) signal(SIGCHLD,reaper);
	(void) signal(SIGINT,delete_LOCKSHM);
	(void) signal(SIGUSR2,read_public_channel);

	while(1)
	{
		clilen = sizeof(cli_addr);
		bzero( (char *)&cli_addr,clilen );

		ssock = accept(msock,(struct sockaddr *)&cli_addr, &clilen);
		
		if(ssock < 0)	
			err_dump("server: accept error\n");
			
		read_SHMSERV();

		int new_idx = login(cli_addr,ssock);

		// fork child process for incoming user
		user[new_idx].user_pid = fork();

		if(user[new_idx].user_pid < 0)	
			err_dump("server: fork error\n");
		else if (user[new_idx].user_pid == 0){  // child
		
			for(int i=0;i<30;i++)
			{
				if(user[i].is_alive && i != new_idx)
					close(user[i].user_sockfd);
			}

			close(msock);
			server_function(new_idx);
			exit(0);
		}
		else{  // parent
	
			int sem = semget(LOCKSTART,1,0666);
			
			write_SHMSERV();
			V(sem);
		}
	}
	// int alive_num = 0;
	// for(int i=0; i < 30; i++){
	// 	if(user[i].is_alive == true)
	// 		alive_num++;
	// }

	// cout << "alive_num = " << alive_num << endl;
	// if(alive_num == 1)
	// 	remove("opened_public_file.txt");
	return 0;
}

void init()
{
	extern char ** environ;
	for ( int i=0; environ[i] != (char *) 0; i++)
	{
		char* env = strtok(environ[i], "=");
		unsetenv(env);
	}

	setenv("PATH","bin:.",1);

	chdir("ras");
	umask(0);
}

int server_function(int idx)
{	
	(void) signal(SIGCHLD, SIG_DFL);
	(void) signal(SIGUSR2, read_public_channel);
	(void) signal(SIGINT, SIG_DFL);

	MYFD = user[idx].user_sockfd;
	MYSHM = SHMSERV + (user[idx].user_id);
	MYLOCK = LOCKSERV + (user[idx].user_id);

	int sem;
	int childpid;	
	
	string line;
	string cmd_str;
	stringstream ss_line;

	char buffer[MSG_MAX];
	char in_fifo_name[11];
	char out_fifo_name[11];
	
	int fifo_in;
	int fifo_out;
	bool is_read = false;
	
	for(int i=0; i < 3; i++)
	{
		close(i);
		dup(user[idx].user_sockfd);
	}

	sem = semget(LOCKSTART, 1, 0666);

	P(sem);

	user[idx].print_welcome_message();

	memset(buffer, 0, MSG_MAX);

	sprintf(buffer, "*** User '%s' entered from %s/%d. ***\r\n", user[idx].user_name, (user[idx].user_ip).c_str(), user[idx].user_port);
	
	broadcast(idx, buffer);

	write(user[idx].user_sockfd, "% ", 2);

	while(1)
	{
		ss_line.str("");
		ss_line.clear();
		cmd_str = "";

		getline(cin,line);

		read_SHMSERV();

		if(line[line.size()-1] == '\r')
			line = line.substr(0,line.size()-1);

		user[idx].num_line++;
		user[idx].num_line = user[idx].num_line % 1000;
		
		ss_line << line;
		ss_line >> cmd_str;

		if(cmd_str == "exit"){

			memset(buffer,0,MSG_MAX);
			sprintf(buffer,"*** User '%s' left. ***\r\n",user[idx].user_name);
			broadcast(idx,buffer);			
			read_SHMSERV();
			user[idx].is_alive = false;
			write_SHMSERV();
			return 0;
		}
		else if(cmd_str == "setenv"){

			string env, p;
			ss_line >> env >> p;
			setenv(env.c_str(),p.c_str(),1);
			write(user[idx].user_sockfd,"% ",2);
			unsigned pos = p.find(":");
			if(pos == -1){
				user[idx].env_vars.clear();
				user[idx].env_vars.insert(p);
			}
			else{
				int start_idx = 0;
				while(start_idx < p.length()){
					if(pos == -1)
						pos = p.length();
					string tmp = p.substr(start_idx, pos-start_idx);
					user[idx].env_vars.insert(tmp);
					start_idx = pos+1;
					pos = p.find(":");
				}
			}

			continue;
		}
		else if(cmd_str == "printenv" ){

			memset(buffer, 0, MSG_MAX);

			while(ss_line >> cmd_str){
				
				string path;
				for(set<string>::iterator it = user[idx].env_vars.begin(); it != user[idx].env_vars.end();){
					
					path = path + *it;
					++it;
					if(it != user[idx].env_vars.end())
						path = path + ":";
					else
						break;
				}

				if(path != ""){
					sprintf(buffer, "%s=%s\r\n", cmd_str.c_str(), path.c_str());
					write(user[idx].user_sockfd, buffer, strlen(buffer));
				}
				else{
					sprintf(buffer, "%s %s", "Can't find ", cmd_str.c_str());
					write(user[idx].user_sockfd, buffer, strlen(buffer));
				}
			} 
			ss_line.str("");
			ss_line.clear();
			
			write(user[idx].user_sockfd, "% ", 2);
			continue;
		}
		else if(cmd_str == "tell"){

			int to_pipe_num;
			ss_line >> to_pipe_num;
			
			if(!user[to_pipe_num-1].is_alive){

				memset(buffer, 0, MSG_MAX);
				sprintf(buffer,"*** Error: user #%d does not exist yet. ***\r\n", to_pipe_num);
				write(user[idx].user_sockfd, buffer, strlen(buffer));
				write(user[idx].user_sockfd,"% ",2);
				continue;
			}
			else{

				unsigned pos = line.find(' '); // tell' 'id 
				pos++;  // start from next char
				pos = line.find(' ',pos);	// tell id''msg
				pos++;  // start from next char
				string message_to_tell = line.substr(pos);

				memset(buffer,0,MSG_MAX);
				strncpy(buffer,message_to_tell.c_str(),MSG_MAX);
				
				char msg[sizeof(buffer)+100];
				memset(msg,0,sizeof(msg));
				sprintf(msg,"*** %s told you ***: %s\r\n",user[idx].user_name, buffer);
				write_public_channel(to_pipe_num, msg);;
				
				write(user[idx].user_sockfd, "% ", 2);
				continue;
			}
		}
		else if(cmd_str == "yell"){

			unsigned pos = line.find(' ');
			pos++;  // start from next char
			string message_to_yell = line.substr(pos);

			memset(buffer, 0, MSG_MAX);
			strncpy(buffer, message_to_yell.c_str(), MSG_MAX-1);

			char msg[strlen(buffer)+100];
			memset(msg,0,strlen(msg));

			sprintf(msg, "*** %s yelled ***: %s\r\n",user[idx].user_name, buffer);
			broadcast(idx, msg);

			write(user[idx].user_sockfd, "% ", 2);
			continue;
		}
		else if(cmd_str == "name"){

			char name_str[20];
			memset(name_str,0,20);

			ss_line >> name_str;
			
			bool no_repeat = true;

			for(int i=0; i < 30; i++){

				if(!strcmp(user[i].user_name, name_str) && i != ((user[idx].user_id)-1) && user[i].is_alive){
					no_repeat = false;
					memset(buffer, 0, MSG_MAX);
					sprintf(buffer, "*** User '%s' already exists. ***\r\n", user[i].user_name);
					write(user[idx].user_sockfd, buffer, strlen(buffer));
					break;
				}
			}
			if(no_repeat){

				read_SHMSERV();
				memset(user[idx].user_name, 0, 20);
				strcpy(user[idx].user_name, name_str);
				write_SHMSERV();

				memset(buffer, 0, MSG_MAX);
				sprintf(buffer, "*** User from %s/%d is named '%s'. ***\r\n", (user[idx].user_ip).c_str(), user[idx].user_port, user[idx].user_name);
				broadcast(idx, buffer);
			}	
			write(user[idx].user_sockfd,"% ",2);
			continue;
		}
		else if(cmd_str == "who"){

			memset(buffer,0,MSG_MAX);
			sprintf(buffer,"<ID>\t<nickname>\t<IP/port>\t<indicate me>\r\n");
			write(user[idx].user_sockfd, buffer, strlen(buffer));
	
			for(int i=0; i < 30; i++)
			{
				if(user[i].is_alive)
				{
					memset(buffer, 0, MSG_MAX);
					sprintf(buffer, "%d\t%s\t%s/%d\t", user[i].user_id, user[i].user_name, (user[i].user_ip).c_str(), user[i].user_port);
					write(user[idx].user_sockfd, buffer, strlen(buffer));

					if(i == (user[idx].user_id)-1)
						write(user[idx].user_sockfd, "<-me\r\n", 6);
					else
						write(user[idx].user_sockfd, "\r\n", 2);
				}
			}
			write(user[idx].user_sockfd, "% ", 2);
			continue;
		}


		bool is_first_cmd = true;
		bool still_finding = false;
		do{
			
			bool wrong_cmd = false;
			CMD cmd;
			// input
			if(is_first_cmd){

				unsigned pos = line.find('<');
				if(pos != -1){

					still_finding = true;
					stringstream ss(line.substr(pos+1));
					ss >> cmd.in_from;
				}
			}

			stringstream ss_cmd;

			ss_cmd.str("");
			ss_cmd.clear();

			ss_cmd << cmd_str << endl;

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
					break;
				}					
				else if(cmd_str[0] == '|' || cmd_str[0] =='!' || cmd_str[0]=='>'){
				
					char c;
					stringstream ss;
					ss << cmd_str;
					
					ss >> c >> cmd.line_shift;
				
					if(cmd_str[0] == '|')
						cmd.out_type = 1;
					else if(cmd_str[0] == '!')
						cmd.out_type = 2;
					else
						cmd.out_type = 4;
	
					break;
				}
				else if(cmd_str[0] == '<'){

					still_finding = false;
				}
				else{

					ss_cmd << cmd_str << endl;
					cmd.argc++;
				}
			}

			if(is_first_cmd && still_finding)
				ss_line >> cmd_str;

			is_first_cmd = false;		
			
			cmd.argv = new char*[cmd.argc+1];

			for(int i=0; i < cmd.argc; i++){

				string arg;
				ss_cmd >> arg;
				char* cstr = new char[arg.size()+1];
				strcpy(cstr, arg.c_str());
				cmd.argv[i] = cstr;
			}
			cmd.argv[cmd.argc] = NULL;
			
			
			char in_from_name[20];

			// input
			if(cmd.in_from > 0){

				int current_idx;
				bool exist = false;
				
				DIR *dir;
				struct dirent *ent;
				string prev_dir = "..";
				if((dir = opendir((prev_dir.c_str() )) ) != NULL){

					while((ent = readdir(dir)) != NULL){
						string filestr(ent->d_name);
						
						char num[3];
						sprintf(num, "%d", cmd.in_from);
						string n(num);
						string filecmp = "public_pipe_" + n + ".txt";
						
						if(filestr == filecmp){
							exist = true;
							break;
						}

					}
						
				}

				int record_public_file = open("../opened_public_file.txt", O_APPEND|O_RDWR, 0777);
				if(record_public_file < 0){
					//cout << "file open fail" << endl;
				}
				ifstream ifs("../opened_public_file.txt", std::ifstream::in);
				string line;
				bool is_gone = false;
				while(getline(ifs, line)){
					stringstream ss_num;
					ss_num << line;
					int fnum;
					ss_num >> fnum;
				
					if(fnum == cmd.line_shift){
						is_gone = true;
						break;
					}

				}

				if(is_gone == true){
					sprintf(buffer,"*** Error: public pipe #%d does not exist yet. ***\r\n", cmd.in_from);
					write(user[idx].user_sockfd,buffer,strlen(buffer));
					ifs.close();
					break;
				}
				else if(exist == false){
					sprintf(buffer,"*** Error: public pipe #%d does not exist yet. ***\r\n", cmd.in_from);
					write(user[idx].user_sockfd,buffer,strlen(buffer));
					ifs.close();
					break;
				}
				else{
					char filename[22];
					sprintf(filename, "../public_pipe_%d.txt", cmd.in_from);
					fifo_in = open(filename, O_CREAT|O_RDONLY, 0777);
					is_read = true;
					sprintf(buffer,"%d\n", cmd.in_from);
					write(record_public_file,buffer,strlen(buffer));
					// public_pipe[current_idx].is_gone = true;
				}
				ifs.close();
				
					
			}

			// outupt
			int file_fd;
			int filepipe[2];
			PIPE new_pipe;
			int dest;
			char destName[20];
			bool many_to_dest;
			
			if(cmd.out_type == 3){  //   >

				int mask = umask(0);

				if((file_fd = open(cmd.file_name.c_str(),O_WRONLY|O_CREAT|O_TRUNC))< 0)
				{
					umask(mask);
					memset(buffer,0,MSG_MAX);
					sprintf(buffer,"can't open file %s (for >)\r\n",cmd.file_name.c_str());
					err_dump(buffer);			
				}
				umask(mask);
			}
			else if(cmd.out_type == 4)   //  >N
			{
				int current_idx;
				bool exist = false;

				DIR *dir;
				struct dirent *ent;
				string prev_dir = "..";
				if((dir = opendir((prev_dir.c_str() )) ) != NULL){

					while((ent = readdir(dir)) != NULL){
						string filestr(ent->d_name);
						char num[3];
						sprintf(num, "%d", cmd.line_shift);
						string n(num);
					
						string filecmp = "public_pipe_" + n + ".txt";
						if(filestr == filecmp){
							exist = true;
							break;
						}
					}	
				}

				ifstream ifs("../opened_public_file.txt", std::ifstream::in);
				string line;
				bool is_gone = false;
				while(getline(ifs, line)){
					stringstream ss_num;
					ss_num << line;
					int fnum;
					ss_num >> fnum;
					if(fnum == cmd.line_shift){
						is_gone = true;
						break;
					}
				}
				if(is_gone == true){
					sprintf(buffer,"*** Error: public pipe #%d already exists ***\r\n", cmd.line_shift);
					write(user[idx].user_sockfd, buffer, strlen(buffer));
					ifs.close();
					break;
				}
				else if(!exist){
					char filename[22];
					sprintf(filename, "../public_pipe_%d.txt", cmd.line_shift);
					fifo_out = open(filename, O_CREAT|O_WRONLY, 0777);
				}
				else{
					sprintf(buffer,"*** Error: public pipe #%d already exists ***\r\n", cmd.line_shift);
					write(user[idx].user_sockfd, buffer, strlen(buffer));
					ifs.close();
					break;
				}
				ifs.close();
			}
			else if((cmd.out_type == 1 || cmd.out_type == 2) && cmd.line_shift == 0){	// cmd1 | cmd2 or cmd1 ! cmd2 
			
				if(pipe(new_pipe.ps) < 0)
					err_dump("can't create pipes.\r\n");
				else
					new_pipe.is_alive = true;
			}
			else if((cmd.out_type == 1 || cmd.out_type == 2) && cmd.line_shift != 0){	//  cmd |N
			
				dest = (user[idx].num_line + cmd.line_shift) % 1000; 
					
				if (!user[idx].pipes[dest].is_alive)
				{
					many_to_dest = false;

					if(pipe(user[idx].pipes[dest].ps) < 0)
						err_dump("can't create pipes.\r\n");

					user[idx].pipes[dest].is_alive = true;
				}
				else
					many_to_dest = true;					
			}

			// fork			
			if((childpid=fork()) < 0)
				err_dump("cant fork\r\n");
			else if(childpid == 0) // child
			{
				// set input
				close(0);

				if(user[idx].pipes[user[idx].num_line].is_alive )
					dup(user[idx].pipes[user[idx].num_line].ps[0]);
				else if(cmd.in_from > 0)
				{		
					dup(fifo_in);
					close(fifo_in);
				}
				else
					dup(user[idx].user_sockfd);

				// set output
				close(1);

				if(cmd.out_type == 0)
					dup(user[idx].user_sockfd);
				else if(cmd.out_type == 3){  //  >
				
					dup(file_fd);
					close(file_fd);		
				}
				else if(cmd.out_type == 4){
									
					dup(fifo_out);
					close(fifo_out);
				}
				else if((cmd.out_type == 1 || cmd.out_type == 2) && cmd.line_shift == 0){  //  cmd1 | cmd2
				
					dup(new_pipe.ps[1]);
					close(new_pipe.ps[0]);
					close(new_pipe.ps[1]);						
				}
				else if((cmd.out_type == 1 || cmd.out_type == 2) && cmd.line_shift != 0){   // cmd |N
				
					dup(user[idx].pipes[dest].ps[1]);
				}
				else{
					dup(user[idx].user_sockfd);
				}

				// set error
				close(2);
				if(cmd.out_type == 2 || cmd.out_type == 4)
					dup(1);
				else
					dup(user[idx].user_sockfd);

				for(int i=0; i<1000; i++)
				{	
					user[idx].pipes[i].clear();
				}

				// find where the command execution file is in
				DIR *dir;
        		struct dirent *ent;
        		bool is_known = false;
        		string cmd_name(cmd.argv[0]);
        		string path_to_append;
        		string concat_cmd_path;   // put it in the first argument of execvp function

        		chdir("ras");
        		for(set<string>::iterator it = user[idx].env_vars.begin(); it != user[idx].env_vars.end(); ++it){
	        
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

				int int_execvp;
				if((int_execvp = execvp(concat_cmd_path.c_str(), cmd.argv)) < 0)
				{
					memset(buffer, 0, MSG_MAX);
					sprintf(buffer, "Unknown command: [%s].\r\n", cmd.argv[0]);
					write(user[idx].user_sockfd,buffer,strlen(buffer));

					close(0); 
					close(1);
					close(2);
					exit(-1);
				}
			}
			else	// parent
			{
				user[idx].pipes[user[idx].num_line].clear();
				
				if(cmd.out_type == 3)	 //  >
					close(file_fd);	
				else if((cmd.out_type == 1 || cmd.out_type == 2) && cmd.line_shift == 0){  //  cmd1 | cmd2
				
					user[idx].pipes[user[idx].num_line].ps[0] = new_pipe.ps[0];
					user[idx].pipes[user[idx].num_line].ps[1] = new_pipe.ps[1];
					user[idx].pipes[user[idx].num_line].is_alive = new_pipe.is_alive = true;
				}
				else if(cmd.out_type == 4){  //   >N
					close(fifo_out);
				}
			
				int status;
				while(waitpid(childpid, &status, WNOHANG) != childpid);

				if(cmd.in_from > 0)
				{
					chdir("..");
					int mask = umask(0);		
					close(fifo_in);
					remove(in_fifo_name);

					umask(mask);
					chdir("ras");
				}

				if(WEXITSTATUS(status) != 0){

					user[idx].pipes[user[idx].num_line].clear();
						
					if((cmd.out_type == 1 || cmd.out_type == 2) && cmd.line_shift != 0 ){

						if (many_to_dest == false)
							user[idx].pipes[dest].clear();
					}
					ss_cmd.str("");
					ss_cmd.clear();
					ss_line.str("");
					ss_line.clear();

					break;
				}
				else{
					
					if (cmd.in_from>0){

						memset(buffer,0,MSG_MAX);
						sprintf(buffer,"*** %s (#%d) just received via %s ***\r\n", user[idx].user_name, user[idx].user_id, in_from_name, cmd.in_from, line.c_str());
						broadcast(idx,buffer);
					}
					if (cmd.out_type == 4){

						memset(buffer,0,MSG_MAX);
						sprintf(buffer,"*** %s (#%d) just piped '%s' ***\r\n", user[idx].user_name, user[idx].user_id, line.c_str(), destName, dest);
						broadcast(idx,buffer);
					}
					
					if(is_read == true){
						char num[3];
						char file_rmv[22];
						sprintf(file_rmv, "../public_pipe_%d.txt", cmd.in_from);
						int stu = remove(file_rmv);
						// if(stu < 0)
						// 	cout << "remove file fails" << endl;
						// else
						// 	cout << "file removed success" << endl;
					}
				}

			}
		} while(ss_line >> cmd_str);
		
		write(user[idx].user_sockfd, "% ", 2);
	}
	return 1;
}


void err_dump(const char* msg)
{
	write(2,msg,strlen(msg));
	exit(1);
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
	if((sockfd = socket(AF_INET,SOCK_STREAM,0)) <0)
		err_dump("server: can't open stream socket\n");
	//  Bind out locat address so that the client can send to us
	if(bind(sockfd,(struct sockaddr *)&serv_addr,servlen) <0)
		err_dump("server: can't bind local address\n");
	
	listen(sockfd,QLEN);
	
	return sockfd;
}

void broadcast(int idx, char* msg)
{
	for(int i=0; i < 30;i++)
	{
		if(i == idx)
			write(user[idx].user_sockfd, msg, strlen(msg));
		else if(user[i].is_alive)
			write_public_channel(i+1, msg);
	}
}

int login( struct sockaddr_in cli_addr, int sockfd)
{
	// process user login idx of user
	// determine idx and assign id
	int idx;
	int id;
	for(int i=0; i < 30; i++)
	{
		if(!user[i].is_alive)
		{
			idx = i;
			id = i+1;
			break; 
		}
	}
	// do user init
	user[idx].user_init(cli_addr, sockfd, id);
	user[idx].is_alive = true;	

	return idx;
}

void reaper(int sig)
{
	int childpid;

	childpid = wait3(NULL,WNOHANG,(struct rusage *)0);
	
	for(int i=0; i<30;i++){

		if(user[i].user_pid == childpid){

			read_SHMSERV();
			close(user[i].user_sockfd);
			user[i].on_exit();
			write_SHMSERV();
			break;

		}
	}
}

void read_SHMSERV()
{
	int sem = semget(LOCKSERV,1,0666);
	P(sem);
	int shmid = shmget(SHMSERV,SHMSIZE,0666);
	char* shm = (char*)shmat(shmid,(void*)0,0);

	//clear before update
	for(int i=0; i<30; i++)
		user[i].is_alive = false;
	stringstream ssshm;
	ssshm << shm;

	shmdt(shm);
	V(sem);

	int uid, pid, port;
	string name, ip;
	while (ssshm >> uid)
	{
		ssshm >> pid;
		getline(ssshm, name);
		getline(ssshm, ip);
		ssshm >>  port;

		user[uid-1].is_alive = true;
		user[uid-1].user_id = uid;
		user[uid-1].user_pid = pid;
		memset(user[uid-1].user_name, 0, 20);
		strcpy(user[uid-1].user_name, name.c_str());
		user[uid-1].user_ip = ip;
		user[uid-1].user_port = port;
	}
}

void write_SHMSERV()
{
	int sem = semget(LOCKSERV,1,0666);
	P(sem);
	int shmid = shmget(SHMSERV,SHMSIZE,0666);
	char* shm = (char*)shmat(shmid,(void*)0,0);

	memset(shm,0,SHMSIZE);	// clear
	char line[MSG_MAX];
	for(int i=0; i<30; i++){

		if (user[i].is_alive){

			memset(line,0,MSG_MAX); 
			sprintf(line,"%d\n%d%s\n%s\n%d\n",user[i].user_id, user[i].user_pid, user[i].user_name, (user[i].user_ip).c_str(), user[i].user_port);
			strcat(shm,line);
		}
	}
	shmdt(shm);
	V(sem);
}

void read_public_channel(int sig)
{
	int sem = semget(MYLOCK,1,0666);
	int shmid = shmget(MYSHM,SHMSIZE,0666);
	char* shm = (char*)shmat(shmid,(void*)0,0);

	char msg[MSG_MAX+1];
	memset(msg,0,MSG_MAX+1);
	strcpy(msg,shm);

	write(MYFD,msg,strlen(msg));
	//clear after read
	memset(shm,0,SHMSIZE);
	*shm = '!';
	shmdt(shm);
	V(sem);
}

void write_public_channel(int id, char* msg)
{
	int sem = semget(LOCKSERV+id, 1, 0666);
	P(sem);
	int shmid = shmget(SHMSERV+id, SHMSIZE, 0666);

	char* shm = (char*)shmat(shmid, (void*)0, 0);
	memset(shm, 0, SHMSIZE);
	strcpy(shm, msg);

	kill(user[id-1].user_pid, SIGUSR2);

	while((*shm) != '!')
		;
	memset(shm,0, SHMSIZE);	
	shmdt(shm);

}

void P(int s)
{
	struct sembuf sop;
	sop.sem_num = 0;
	sop.sem_op = -1;
	sop.sem_flg = 0;

	semop(s,&sop,1);
}

void V(int s)
{
	struct sembuf sop;
	sop.sem_num = 0;
	sop.sem_op = 1;
	sop.sem_flg = 0;

	semop(s,&sop,1);
}

void create_LOCKSHM()
{
	int sem = semget(LOCKSERV, 1, IPC_CREAT|IPC_EXCL|0666);
	semctl(sem, 0, SETVAL, 1);

	sem = semget(LOCKSTART, 1, IPC_CREAT|IPC_EXCL|0666);
	semctl(sem, 0, SETVAL, 0);


	for(int i=0; i < 30; i++){

		sem = semget((LOCKSERV+i+1), 1, IPC_CREAT|IPC_EXCL|0666);
		semctl(sem, 0, SETVAL, 1);
	}

	int shmid = shmget(SHMSERV, SHMSIZE, IPC_CREAT|0666);

	for(int i=0; i < 30; i++)
		shmid = shmget((SHMSERV+i+1), SHMSIZE, IPC_CREAT|0666);
}

void delete_LOCKSHM(int sig)
{
	int sem = semget(LOCKSERV,1,0666);
	semctl(sem,0, IPC_RMID, 0);

	sem= semget(LOCKSTART,1,0666);
	semctl(sem, 0, IPC_RMID, 0);

	for(int i=0; i < 30; i++){

		sem = semget((LOCKSERV+i+1), 1, 0666);
		semctl(sem, 0, IPC_RMID, 0);
	}

	int shmid = shmget(SHMSERV, SHMSIZE, 0666);
	shmctl(shmid, IPC_RMID, NULL);

	for(int i=0; i < 30; i++){

		shmid = shmget((SHMSERV+i+1), SHMSIZE, 0666);
		shmctl(shmid, IPC_RMID, NULL);
	}

	exit(0);
}



