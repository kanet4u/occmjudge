/*
* CDAC DAC1 OCCM Project
*	OCCM Server
*/
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <mysql/mysql.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/resource.h>

// Global constants
#define BUFFER_SIZE 1024
#define LOCKFILE "/var/run/occmserver.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define STD_MB 1048576

//Messages
#define OCCM_WT0 0
#define OCCM_WT1 1
#define OCCM_CI 2
#define OCCM_RI 3
#define OCCM_AC 4
#define OCCM_PE 5
#define OCCM_WA 6
#define OCCM_TL 7
#define OCCM_ML 8
#define OCCM_OL 9
#define OCCM_RE 10
#define OCCM_CE 11
#define OCCM_CO 12


// Global static variables, from conf file
static int DEBUG=0;
static char host_name[BUFFER_SIZE];
static char user_name[BUFFER_SIZE];
static char password [BUFFER_SIZE];
static char db_name  [BUFFER_SIZE];
static char occm_home  [BUFFER_SIZE];
static char occm_lang_set  [BUFFER_SIZE];
static int port_number;
static int max_running;
static int sleep_time;
static int sleep_tmp;
int test;


static bool STOP=false;

static MYSQL *conn;
static MYSQL_RES *res;
static MYSQL_ROW row;
static char query[BUFFER_SIZE];

void call_for_exit(int s)
{
   STOP=true;
   printf("Stopping occmserver...\n");
}

int after_equal(char * c){
	int i=0;
	for(;c[i]!='\0'&&c[i]!='=';i++);
	return ++i;
}

void trim(char * c){
    char buf[BUFFER_SIZE];
    char * start,*end;
    strcpy(buf,c);
    start=buf;
    while(isspace(*start)) start++;
    end=start;
    while(!isspace(*end)) end++;
    *end='\0';
    strcpy(c,start);
}

bool read_buf(char * buf,const char * key,char * value){
	if (strncmp(buf,key, strlen(key)) == 0) {
		strcpy(value, buf + after_equal(buf));
		trim(value);	
		if (DEBUG) 
			printf("%s = %s\n",key,value);
		return 1;
	}
	return 0;
}
void read_int(char * buf,const char * key,int * value){
	char buf2[BUFFER_SIZE];
	if (read_buf(buf,key,buf2))
		sscanf(buf2, "%d", value);
		
}

void write_log(const char *fmt, ...)
{
	va_list         ap;
	char            buffer[4096];
	sprintf(buffer,"%s/log/client.log",occm_home);
	FILE *fp = fopen(buffer, "a+");
	if (fp==NULL){
		 fprintf(stderr,"openfile error!\n");
		 system("pwd");
	}va_start(ap, fmt);
	vsprintf(buffer, fmt, ap);
	fprintf(fp,"%s\n",buffer);
	//if (DEBUG) 
	//printf("%s\n",buffer);
	va_end(ap);
	fclose(fp);
}

// read the configue file
void init_mysql_conf() {
	FILE *fp=NULL;
	char buf[BUFFER_SIZE];
	host_name[0]=0;
	user_name[0]=0;
	password[0]=0;
	db_name[0]=0;
	port_number=3306;
	max_running=3;
	sleep_time=1;

	strcpy(occm_lang_set,"0,1,2,3");
	fp = fopen("./etc/occm.conf", "r");
	if(fp!=NULL){
		while (fgets(buf, BUFFER_SIZE - 1, fp)) {
			read_buf(buf, "OCCM_HOST_NAME",host_name);	
			read_buf(buf, "OCCM_USER_NAME",user_name);
			read_buf(buf, "OCCM_PASSWORD",password);
			read_buf(buf, "OCCM_DB_NAME",db_name);
			read_int(buf, "OCCM_PORT_NUMBER", &port_number);
			read_int(buf, "OCCM_RUNNING", &max_running);
			read_int(buf, "OCCM_SLEEP_TIME", &sleep_time);
			read_buf(buf, "OCCM_LANG_SET",occm_lang_set);
		}

		sprintf(query,"SELECT id, test FROM submissions WHERE language_id in (%s) and status<2 ORDER BY status ASC, id ASC limit %d",occm_lang_set,max_running*2);
		sleep_tmp=sleep_time;
    }
}

void run_client(int runid, int istest, int clientid){
	char buf[BUFFER_SIZE],runidstr[BUFFER_SIZE], isteststr[BUFFER_SIZE];
	struct rlimit LIM;
	
	LIM.rlim_max=800;
	LIM.rlim_cur=800;// send SIGKILL after 800 seconds
	setrlimit(RLIMIT_CPU,&LIM);

	LIM.rlim_max=80*STD_MB;
	LIM.rlim_cur=80*STD_MB;
	setrlimit(RLIMIT_FSIZE,&LIM); // The maximum size of file the process can create.

	LIM.rlim_max=STD_MB<<11;
	LIM.rlim_cur=STD_MB<<11;
	setrlimit(RLIMIT_AS,&LIM); // The maximum size of the process's virtual memory (address space) in bytes.

	LIM.rlim_cur=LIM.rlim_max=200;
	setrlimit(RLIMIT_NPROC, &LIM); // The maximum number of processes (or, more precisely on Linux, threads) that can be created for the real user ID of the calling process

	// We can use also:
		// RLIMIT_STACK - The maximum size of the process stack, in bytes
		// RLIMIT_NOFILE - Specifies a value one greater than the maximum file descriptor number that can be opened by this process.
		// RLIMIT_DATA - The maximum size of the process's data segment (initialized data, uninitialized data, and heap).
		// RLIMIT_CORE - Maximum size of core file. When 0 no core dump files are created. 


	sprintf(runidstr,"%d",runid);
	sprintf(buf,"%d",clientid);
	sprintf(isteststr, "%d", istest);

	//if (!DEBUG)
		//execl("/usr/bin/judge_client","/usr/bin/judge_client",runidstr,buf,oj_home,(char *)NULL);
	//else
	if(!istest)
	{
		printf("occmclient %s %s %s",runidstr,buf,occm_home);
		execl("./client/occmclient","./client/occmclient",runidstr,buf,occm_home,(char *)NULL);		
	}
	else
	{
		printf("occmclient %s %s %s test",runidstr,buf,occm_home);
		execl("./client/occmclient","./client/occmclient",runidstr,buf,occm_home,"test",(char *)NULL);
	}
	// occmclient 1002 0 /home/occm test 
	//      0       1  2      3       4   
	//printf("Run client!: ./client/occmclient %s %s %s\n", runidstr,buf,occm_home);

	//exit(0);
}

int executesql(const char * sql){
	if (mysql_real_query(conn,sql,strlen(sql))){
		if(DEBUG)write_log("%s", mysql_error(conn));
		sleep(20);
		conn=NULL;
		return 1;
	}else
	  return 0;
}

int init_mysql(){
	if(conn==NULL){
		conn=mysql_init(NULL);		// init the database connection
		/* connect the database */
		const char timeout=30;
		mysql_options(conn,MYSQL_OPT_CONNECT_TIMEOUT,&timeout);

		if(!mysql_real_connect(conn,host_name,user_name,password, db_name,port_number,0,0)){// connect to the db
			if(DEBUG)
				write_log("%s", mysql_error(conn));
			sleep(2);
			return 1;
		}
		else{
			return 0;
		}
	}
	else{
	    return executesql("set names utf8");// set charset
	}
}

int get_jobs(int * jobs, int * istest){
	int i=0;
        int ret=0;

        if (mysql_real_query(conn, query, strlen(query)))
	{
                if(DEBUG)
			write_log("%s", mysql_error(conn));
                sleep(20);
                return 0;
        }
        res=mysql_store_result(conn);

        while((row=mysql_fetch_row(res))!=NULL)
	{
                jobs[i]=atoi(row[0]);
		istest[i++]=atoi(row[1]);
        }
        ret=i;
        while(i <= max_running*2) 
		jobs[i++]=0;
        return ret; // job count to compile and run
}


bool check_out(int solution_id,int result){
	char sql[BUFFER_SIZE];
	sprintf(sql,"UPDATE submissions SET status=%d,runtime=0,memory=0,updation_time=NOW() WHERE id=%d and status<2 LIMIT 1"
			,result,solution_id);
	if (mysql_real_query(conn,sql,strlen(sql))){
		syslog(LOG_ERR | LOG_DAEMON, "%s",mysql_error(conn));
		return false;
	}else{
		if(mysql_affected_rows(conn)>0ul)
			return true;
		else
			return false;
	}	
}

// get works from db and runs client
int work()
{
        static  int retcnt=0;
        int i=0;
        static pid_t ID[100];
        static int workcnt=0;
        int runid=0;
	int runid_istest=0;
        int  jobs[max_running*2+1];
	int  istest[max_running*2+1];
        pid_t tmp_pid=0;

        if(!get_jobs(jobs, istest)) 
		retcnt = 0;
// print jobs

        /* exec the submit */
        for (int j=0;jobs[j]>0;j++){
                runid=jobs[j];
                runid_istest=istest[j];	

                if(DEBUG){
			write_log("Judging solution %d",runid);
		}
                if (workcnt >= max_running){              // if no more client can running
                        tmp_pid=waitpid(-1,NULL,0);     // wait 4 one child exit
                        workcnt--;
			retcnt++;
                        for (i=0;i<max_running;i++)     // get the client id
                                if (ID[i]==tmp_pid) break; // got the client id
                        ID[i]=0;
                }else{                                                  // have free client

                        for (i=0;i<max_running;i++)     // find the client id
                                if (ID[i]==0) break;    // got the client id
                }
                if(workcnt<max_running && check_out(runid,OCCM_CI)){ // set state as a compiling
                        workcnt++;
                        ID[i]=fork();                                   // start to fork
                        if (ID[i]==0){
                                if(DEBUG){
					printf("<<=sid=%d===clientid=%d==>>\n",runid,i);
					write_log("<<=sid=%d===clientid=%d==>>\n",runid,i);
				}
                                run_client(runid,runid_istest,i);    // if the process is the son, run it
                                exit(0);
                        }

                }else{
                        ID[i]=0;
                }
        }
        while ( (tmp_pid = waitpid(-1,NULL,WNOHANG) )>0){
                        workcnt--;retcnt++;
                        for (i=0;i<max_running;i++)     // get the client id
                                if (ID[i]==tmp_pid) break; // got the client id
                        ID[i]=0;
                        printf("tmp_pid = %d\n",tmp_pid);
        }
        
        mysql_free_result(res);                         // free the memory
        executesql("commit");
        
    if(DEBUG&&retcnt)write_log("<<%ddone!>>",retcnt);

    return retcnt;
}

int lockfile(int fd)
{
	struct flock fl;
	fl.l_type = F_WRLCK;
	fl.l_start = 0;
	fl.l_whence = SEEK_SET;
	fl.l_len = 0;
	return (fcntl(fd,F_SETLK,&fl)); // perform various actions on open lockfile
}

int already_running(){
	int fd;
	char buf[16];
	fd = open(LOCKFILE, O_RDWR|O_CREAT, LOCKMODE);
	if (fd < 0){
		printf("can't open %s: %s", LOCKFILE, strerror(errno));
		syslog(LOG_ERR|LOG_DAEMON, "can't open %s: %s", LOCKFILE, strerror(errno));
		exit(1);
	}
	if (lockfile(fd) < 0){
		if (errno == EACCES || errno == EAGAIN){
			close(fd);
			return 1;
		}
		printf("Can't lock %s: %s", LOCKFILE, strerror(errno));
		syslog(LOG_ERR|LOG_DAEMON, "Can't lock %s: %s", LOCKFILE, strerror(errno));
		exit(1);
	}
	ftruncate(fd, 0);
	sprintf(buf,"%d", getpid());
	write(fd,buf,strlen(buf)+1); // write to LOCKFILE stdout
	return (0);
}
static int serverpid=0;
int daemon_init(void)
{ 
	pid_t pid;

	if((pid = fork()) < 0) 
		return(-1);
	else 
		if(pid != 0) 
			exit(0); /* parent exit */

	/* child continues */
	serverpid=pid;
	setsid(); /* child become session leader */
	chdir(occm_home); /* change working directory */
	umask(0); /* clear file mode creation mask */
	close(0); /* close stdin */
	close(1); /* close stdout */
	close(2); /* close stderr */
	return(0); 
}

int main(int argc, char** argv)
{	
	DEBUG=0;
	test=0;	
	strcpy(occm_home,"/home/occm");
	chdir(occm_home);// change the home dir
	
	if (strcmp(occm_home,"/home/occm")==0 && already_running()){
		FILE * lfp;
		int _pid;
		lfp = fopen (LOCKFILE, "r");
		fscanf(lfp, "%d", &_pid);
		printf("Error: Online Code Competition Management Server is already running! (pid=%d)\n", _pid);
		return 1; 
	}
	printf("Online Code Competition Management Server is starting......[OK]\n");
	//if (!DEBUG) // if not home dir 
	//	daemon_init();
	if (strcmp(occm_home,"/home/occm")==0 && already_running()){
		printf("Error: This daemon program is already running!\n");
		syslog(LOG_ERR|LOG_DAEMON, "This daemon program is already running!\n");
		return 1; 
	}
	
	init_mysql_conf();	// set the database info
	write_log("Mysql init...\n");
	signal(SIGQUIT,call_for_exit);
	signal(SIGKILL,call_for_exit);
	signal(SIGTERM,call_for_exit);
	int j=1;
	while (1)
	{			// start to run
		write_log("listening occm database...\n");
		while(j&&(!init_mysql()))
		{ 	       
			j=work();
		}
		sleep(sleep_time);
		j=1;
	}
	return 0;
}

