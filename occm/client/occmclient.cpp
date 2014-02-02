/*************************************************************
*
*
*	client
*Usage: occmclient submission_id client_id home_dir debug
*
**************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mysql/mysql.h>
#include <assert.h>
#include "okcalls.h"

#define STD_MB 1048576
#define STD_T_LIM 2
#define STD_F_LIM (STD_MB<<5)
#define STD_M_LIM (STD_MB<<7)
#define BUFFER_SIZE 512

//WAITING
#define OCCM_WT0 0 
#define OCCM_WT1 1
//COMPILING
#define OCCM_CI 2
//RUNNING
#define OCCM_RI 3
//ACCEPTED
#define OCCM_AC 4
//PRESENTATION ERROR
#define OCCM_PE 5
//WRONG ANSWER
#define OCCM_WA 6
//TIME LIMIT EXCEEDED
#define OCCM_TL 7
//MEMORY LIMIT EXCEEDED
#define OCCM_ML 8
//OUTPUT LIMIT EXCEEDED
#define OCCM_OL 9
//RUNTIME ERROR
#define OCCM_RE 10
//COMPILATION ERROR
#define OCCM_CE 11
#define OCCM_CO 12
#define OCCM_TR 13

#ifdef __i386
#define REG_SYSCALL orig_eax
#define REG_RET eax
#define REG_ARG0 ebx
#define REG_ARG1 ecx
#else
#define REG_SYSCALL orig_rax
#define REG_RET rax
#define REG_ARG0 rdi
#define REG_ARG1 rsi
#endif


static int DEBUG = 0;
static char host_name[BUFFER_SIZE];
static char user_name[BUFFER_SIZE];
static char password[BUFFER_SIZE];
static char db_name[BUFFER_SIZE];
static char occm_home[BUFFER_SIZE];
static int port_number;
static int max_running;
static int sleep_time;
static int java_time_bonus = 5;
static int java_memory_bonus = 512;
static char java_xms[BUFFER_SIZE];
static char java_xmx[BUFFER_SIZE];
static int sim_enable = 0;
static int oi_mode=0;
static int use_max_time=0;

static int shm_run=0;

static char record_call=0;

MYSQL *conn;

static char lang_ext[4][8] = { "c", "cc", "pas", "java"};

const int call_array_size=512;
int call_counter[call_array_size]={0};
static char LANG_NAME[BUFFER_SIZE];


long get_file_size(const char * filename) {
        struct stat f_stat;

        if (stat(filename, &f_stat) == -1) {
                return 0;
        }
        return (long) f_stat.st_size;
}


int isInFile(const char fname[]) {
        int l = strlen(fname);
        if (l <= 3 || strcmp(fname + l - 3, ".in") != 0)
                return 0;
        else
                return l - 3;
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

void write_log(const char *fmt, ...) {
        va_list ap;
        char buffer[4096];
        //      time_t          t = time(NULL);
        //int l;
        sprintf(buffer,"%s/log/client.log",occm_home);
        FILE *fp = fopen(buffer, "a+");
        if (fp == NULL) {
                fprintf(stderr, "openfile error!\n");
                system("pwd");
        }
        va_start(ap, fmt);
        //l = 
        vsprintf(buffer, fmt, ap);
        fprintf(fp, "%s\n", buffer);
        if (DEBUG)
                printf("%s\n", buffer);
        va_end(ap);
        fclose(fp);

}

bool read_buf(char * buf,const char * key,char * value){
   if (strncmp(buf,key, strlen(key)) == 0) {
                strcpy(value, buf + after_equal(buf));
                trim(value);
                return 1;
   }
   return 0;
}
void read_int(char * buf,const char * key,int * value){
        char buf2[BUFFER_SIZE];
        if (read_buf(buf,key,buf2)){
                sscanf(buf2, "%d", value);
	}
}

// read the configue file
void init_mysql_conf() {
        FILE *fp=NULL;
        char buf[BUFFER_SIZE];
        host_name[0] = 0;
        user_name[0] = 0;
        password[0] = 0;
        db_name[0] = 0;
        port_number = 3306;
        max_running = 3;
        sleep_time = 3;
        strcpy(java_xms, "-Xms32m");
        strcpy(java_xmx, "-Xmx256m");
        sprintf(buf,"%s/etc/occm.conf",occm_home);
        fp = fopen("./etc/occm.conf", "r");
        if(fp!=NULL){
                while (fgets(buf, BUFFER_SIZE - 1, fp)) {
                        read_buf(buf,	"OCCM_HOST_NAME",		host_name);
                        read_buf(buf, 	"OCCM_USER_NAME",		user_name);
                        read_buf(buf, 	"OCCM_PASSWORD",		password);
                        read_buf(buf, 	"OCCM_DB_NAME",			db_name);
                        read_int(buf, 	"OCCM_PORT_NUMBER", 		&port_number);
                        read_int(buf, 	"OCCM_JAVA_TIME_BONUS", 	&java_time_bonus);
                        read_int(buf, 	"OCCM_JAVA_MEMORY_BONUS", 	&java_memory_bonus);
                        read_int(buf, 	"OCCM_SIM_ENABLE", 		&sim_enable);
                        read_buf(buf,	"OCCM_JAVA_XMS",		java_xms);
                        read_buf(buf,	"OCCM_JAVA_XMX",		java_xmx);
                        read_int(buf, 	"OCCM_OI_MODE", 		&oi_mode);
                        read_int(buf, 	"OCCM_SHM_RUN", 		&shm_run);
                        read_int(buf, 	"OCCM_USE_MAX_TIME", 		&use_max_time);
                }
        }
}

void init_parameters(int argc, char ** argv, int & solution_id,int & runner_id, int & istest) {
	if (argc < 3) {
		fprintf(stderr, "Usage:%s solution_id runner_id.\n", argv[0]);
		fprintf(stderr, "Multi:%s solution_id runner_id occm_base_path.\n", argv[0]);
		fprintf(stderr, "Debug:%s solution_id runner_id occm_base_path debug.\n", argv[0]);
		exit(1);
	}
	//DEBUG = (argc > 4);
	//record_call=(argc > 5);
	DEBUG = 0;
	record_call=0;
	
	if(argc > 4){
		//strcpy(LANG_NAME,argv[5]);
		istest=1;
	}
	
	if (argc > 3)
		strcpy(occm_home, argv[3]);
	else
		strcpy(occm_home, "/home/occm");

	chdir(occm_home); // change the dir

	solution_id = atoi(argv[1]);
	runner_id = atoi(argv[2]);
}

int init_mysql_conn() {
        conn = mysql_init(NULL);
        const char timeout = 30;
        mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

        if (!mysql_real_connect(conn, host_name, user_name, password, db_name, port_number, 0, 0)) {
                write_log("%s", mysql_error(conn));
                return 0;
        }
        const char * utf8sql = "set names utf8";
        if (mysql_real_query(conn, utf8sql, strlen(utf8sql))) {
                write_log("%s", mysql_error(conn));
                return 0;
        }
        return 1;
}
int execute_cmd(const char * fmt, ...) {
        char cmd[BUFFER_SIZE];

        int ret = 0;
        va_list ap;

        va_start(ap, fmt);
        vsprintf(cmd, fmt, ap);
        ret = system(cmd);
        va_end(ap);
        return ret;
}

void clean_workdir(char * work_dir ) {
        execute_cmd("umount %s/proc", work_dir);
        if (DEBUG) {
                execute_cmd("mv %s/* %slog/", work_dir, work_dir);
        } else {
                execute_cmd("rm -Rf %s/*", work_dir);
        }
}

void get_solution_info(int solution_id, int & p_id, char * user_id, int & lang) {

	MYSQL_RES *res;
	MYSQL_ROW row;

	char sql[BUFFER_SIZE];
	// get the problem id and user id from Table:solution
	sprintf(
		sql,
		//"SELECT problem_id, user_id, language FROM solution where solution_id=%d",
		"SELECT problem_id, user_id, language_id FROM submissions where id=%d",
		solution_id);
	mysql_real_query(conn, sql, strlen(sql));
	res = mysql_store_result(conn);
	row = mysql_fetch_row(res);
	p_id = atoi(row[0]);
	strcpy(user_id, row[1]);
	lang = atoi(row[2]);
	mysql_free_result(res);
}

void get_problem_info(int p_id, int & time_lmt, int & mem_lmt) {
	 // get the problem info from Table:problem
	char sql[BUFFER_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	sprintf(
	        sql,
	        //"SELECT time_limit, memory_limit FROM problem where problem_id=%d",
		"SELECT time_limit, memory_limit FROM problems where id=%d",
	        p_id);
	mysql_real_query(conn, sql, strlen(sql));
	res = mysql_store_result(conn);
	row = mysql_fetch_row(res);
	time_lmt = atoi(row[0]);
	mem_lmt = atoi(row[1]);	
	mysql_free_result(res);
}

void get_solution(int solution_id, char * work_dir, int lang) {
	char sql[BUFFER_SIZE], src_pth[BUFFER_SIZE];
	// get the source code
	MYSQL_RES *res;
	MYSQL_ROW row;
	//sprintf(sql, "SELECT source FROM source_code WHERE solution_id=%d", solution_id);
	sprintf(sql, "SELECT source_code FROM submissions WHERE id=%d", solution_id);
	printf("RUNNIG QUERY: %s\n", sql);
	mysql_real_query(conn, sql, strlen(sql));
	res = mysql_store_result(conn);
	row = mysql_fetch_row(res);


	// create the src file
	sprintf(src_pth, "Main.%s", lang_ext[lang]);
	if (DEBUG)
		printf("Main=%s", src_pth);
	FILE *fp_src = fopen(src_pth, "w");
	fprintf(fp_src, "%s", row[0]);
	printf("Source code: %s\n", row[0]);
	mysql_free_result(res);
	fclose(fp_src);
}

void get_custominput(int solution_id, char * work_dir) {
	char sql[BUFFER_SIZE], src_pth[BUFFER_SIZE];
	// get the source code
	MYSQL_RES *res;
	MYSQL_ROW row;
	sprintf(sql, "SELECT input FROM submissions WHERE id=%d",
		        solution_id);
	printf("Query custom input: %s\n", sql);
	printf("work dir custom input: %s\n", work_dir);
	mysql_real_query(conn, sql, strlen(sql));
	res = mysql_store_result(conn);
	row = mysql_fetch_row(res);
	if(row!=NULL){
		// create the src file
		sprintf(src_pth, "data.in");
		FILE *fp_src = fopen(src_pth, "w");
		fprintf(fp_src, "%s", row[0]);
		//printf("Custom input: %s\n", row[0]);
		fclose(fp_src);
	}
	mysql_free_result(res);
}

/* write runtime error message back to database */
void _addreinfo_mysql(int solution_id,const char * filename) {
        char sql[(1 << 16)], *end;
        char reinfo[(1 << 16)], *rend;
        FILE *fp = fopen(filename, "r");
        snprintf(sql, (1 << 16) - 1,
                        "DELETE FROM runtimerror WHERE submission_id=%d", solution_id);
        //mysql_real_query(conn, sql, strlen(sql));
        rend = reinfo;
        while (fgets(rend, 1024, fp)) {
                rend += strlen(rend);
                if (rend - reinfo > 40000)
                        break;
        }
        rend = 0;
        end = sql;
        strcpy(end, "INSERT INTO runtimerror VALUES(");
        end += strlen(sql);
        *end++ = '\'';
        end += sprintf(end, "%d", solution_id);
        *end++ = '\'';
        *end++ = ',';
        *end++ = '\'';
        end += mysql_real_escape_string(conn, end, reinfo, strlen(reinfo));
        *end++ = '\'';
        *end++ = ')';
        *end = 0;
        //      printf("%s\n",ceinfo);
	
        //if (mysql_real_query(conn, sql, end - sql))
        //        printf("%s\n", mysql_error(conn));
        fclose(fp);


	sprintf(
        	sql,
        	"UPDATE submissions SET log='%s',updation_time=NOW() WHERE id=%d LIMIT 1%c",reinfo, solution_id, 0);
        printf("UPDATE LOG: %s\n", sql);
        if (mysql_real_query(conn, sql, strlen(sql))) {
                //              printf("..update failed! %s\n",mysql_error(conn));
        }
}

void addreinfo(int solution_id) {
        _addreinfo_mysql(solution_id,"error.out");
}


void addcustomout(int solution_id,const char * filename) {
	char sql[(1 << 16)], *end;
        char reinfo[(1 << 16)], *rend;
        FILE *fp = fopen(filename, "r");
        snprintf(sql, (1 << 16) - 1,
                        "DELETE FROM runtimerror WHERE submission_id=%d", solution_id);
        //mysql_real_query(conn, sql, strlen(sql));
        rend = reinfo;
        while (fgets(rend, 1024, fp)) {
                rend += strlen(rend);
                if (rend - reinfo > 40000)
                        break;
        }
        rend = 0;
        end = sql;
        strcpy(end, "INSERT INTO runtimerror VALUES(");
        end += strlen(sql);
        *end++ = '\'';
        end += sprintf(end, "%d", solution_id);
        *end++ = '\'';
        *end++ = ',';
        *end++ = '\'';
        end += mysql_real_escape_string(conn, end, reinfo, strlen(reinfo));
        *end++ = '\'';
        *end++ = ')';
        *end = 0;
        //      printf("%s\n",ceinfo);
        //if (mysql_real_query(conn, sql, end - sql))
        //        printf("%s\n", mysql_error(conn));
        fclose(fp);
	//printf("UPDATE LOG: %s\n", reinfo);
	//char sql[BUFFER_SIZE];
        sprintf(
        	sql,
        	"UPDATE submissions SET log='%s',updation_time=NOW() WHERE id=%d LIMIT 1%c",reinfo, solution_id, 0);
        printf("UPDATE LOG: %s\n", sql);
        if (mysql_real_query(conn, sql, strlen(sql))) {
                //              printf("..update failed! %s\n",mysql_error(conn));
        }
	//_addreinfo_mysql(solution_id,"user.out");
}

int compile(int lang) {
        int pid;
        const char * CP_C[] = { "gcc", "Main.c", "-o", "Main","-fno-asm","-Wall", "-lm",
                        "--static", "-std=c99", "-DONLINE_JUDGE", NULL };
        const char * CP_X[] = { "g++", "Main.cc", "-o", "Main","-fno-asm", "-Wall",
                        "-lm", "--static","-std=c++0x", "-DONLINE_JUDGE", NULL };

	char javac_buf[4][16];
	char *CP_J[5];
	
    	for(int i=0;i<4;i++) 
		CP_J[i]=javac_buf[i];

        sprintf(CP_J[0],"javac");
        sprintf(CP_J[1],"-J%s",java_xms);
        sprintf(CP_J[2],"-J%s",java_xmx);
        sprintf(CP_J[3],"Main.java");
        CP_J[4]=(char *)NULL;
    
        pid = fork();
        if (pid == 0) {
                struct rlimit LIM;
                LIM.rlim_max = 60;
                LIM.rlim_cur = 60;
                setrlimit(RLIMIT_CPU, &LIM);
                alarm(60);
                LIM.rlim_max = 900 * STD_MB;
                LIM.rlim_cur = 900 * STD_MB;
                setrlimit(RLIMIT_FSIZE, &LIM);

                LIM.rlim_max =  STD_MB<<11;
                LIM.rlim_cur =  STD_MB<<11;
                setrlimit(RLIMIT_AS, &LIM);
                if (lang != 2&& lang != 11) {
                        freopen("ce.txt", "w", stderr);
                } else {
                        freopen("ce.txt", "w", stdout);
                }
                switch (lang) {
		        case 0:
		                execvp(CP_C[0], (char * const *) CP_C);
		                break;
		        case 1:
		                execvp(CP_X[0], (char * const *) CP_X);
		                break;
		        case 3:
		                execvp(CP_J[0], (char * const *) CP_J);
		                break;
			default:
		                printf("nothing to do!\n");
                }
                if (DEBUG)
                        printf("compile end!\n");
                exit(0);
        } else {
                int status=0;
                waitpid(pid, &status, 0);
                if (DEBUG)
                        printf("status=%d\n", status);
                return status;
        }
}

/* write compile error message back to database */
void addceinfo(int solution_id) {
        char sql[(1 << 16)], *end;
        char ceinfo[(1 << 16)], *cend;
        FILE *fp = fopen("ce.txt", "r");
        snprintf(sql, (1 << 16) - 1,
                        "DELETE FROM compilerrors WHERE submission_id=%d", solution_id);
        //mysql_real_query(conn, sql, strlen(sql));
        cend = ceinfo;
        while (fgets(cend, 1024, fp)) {
                cend += strlen(cend);
                if (cend - ceinfo > 40000)
                        break;
        }
        cend = 0;
        end = sql;
        strcpy(end, "INSERT INTO compilerrors VALUES(");
        end += strlen(sql);
        *end++ = '\'';
        end += sprintf(end, "%d", solution_id);
        *end++ = '\'';
        *end++ = ',';
        *end++ = '\'';
        end += mysql_real_escape_string(conn, end, ceinfo, strlen(ceinfo));
        *end++ = '\'';
        *end++ = ')';
        *end = 0;
	printf("Compile error: %s\n", ceinfo);
        //if (mysql_real_query(conn, sql, end - sql))
        //        printf("%s\n", mysql_error(conn));
        fclose(fp);

	sprintf(
        	sql,
        	"UPDATE submissions SET log='%s',updation_time=NOW() WHERE id=%d LIMIT 1%c",ceinfo, solution_id, 0);
        printf("UPDATE LOG: %s\n", sql);
        if (mysql_real_query(conn, sql, strlen(sql))) {
                //              printf("..update failed! %s\n",mysql_error(conn));
        }
}



/* write result back to database */
void _update_solution_mysql(int solution_id, int result, int time, int memory) {
        char sql[BUFFER_SIZE];
        sprintf(
        	sql,
        	"UPDATE submissions SET status=%d,runtime=%d,memory=%d,updation_time=NOW() WHERE id=%d LIMIT 1%c",
        	result, time, memory, solution_id, 0);
        
        if (mysql_real_query(conn, sql, strlen(sql))) {
                //              printf("..update failed! %s\n",mysql_error(conn));
        }
}

void update_solution(int solution_id, int result, int time, int memory) {
        if(result==OCCM_TL&&memory==0) result=OCCM_ML;
	_update_solution_mysql( solution_id,  result,  time,  memory);
}

void update_user(char  * user_id) {
        char sql[BUFFER_SIZE];
        sprintf(
                sql,
                "UPDATE `users` SET `solved`=(SELECT count(DISTINCT `problem_id`) FROM `solution` WHERE `user_id`=\'%s\' AND `result`=\'4\') WHERE `user_id`=\'%s\'",
                user_id, user_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
        sprintf(
                sql,
                "UPDATE `users` SET `submit`=(SELECT count(*) FROM `solution` WHERE `user_id`=\'%s\') WHERE `user_id`=\'%s\'",
                user_id, user_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
}

void update_problem(int p_id) {
        char sql[BUFFER_SIZE];
        sprintf(sql,
                "UPDATE `problem` SET `accepted`=(SELECT count(*) FROM `solution` WHERE `problem_id`=\'%d\' AND `result`=\'4\') WHERE `problem_id`=\'%d\'",
                p_id, p_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
        sprintf(sql,
                "UPDATE `problem` SET `submit`=(SELECT count(*) FROM `solution` WHERE `problem_id`=\'%d\') WHERE `problem_id`=\'%d\'",
                p_id, p_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
}

void init_syscalls_limits(int lang) {
        int i;
        memset(call_counter, 0, sizeof(call_counter));
        if (DEBUG)
                write_log("init_call_counter:%d", lang);
        if (record_call) { // C & C++
                for (i = 0; i<call_array_size; i++) {
                        call_counter[i] = 0;
                }
        }else if (lang <= 1) { // C & C++
                for (i = 0; LANG_CC[i]; i++) {
                        call_counter[LANG_CV[i]] = LANG_CC[i];
                }
        } else if (lang == 3) { // Java
                for (i = 0; LANG_JC[i]; i++)
                        call_counter[LANG_JV[i]] = LANG_JC[i];
        }
}

void run_solution(int & lang, char * work_dir, int & time_lmt, int & usedtime, int & mem_lmt) {
        nice(19);
                // now the user is "judger" and give low priority
        chdir(work_dir);
        // open the files
        freopen("data.in", "r", stdin);
        freopen("user.out", "w", stdout);
        freopen("error.out", "a+", stderr);
        // trace me
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        // run me
        if (lang != 3)
                chroot(work_dir);

        while(setgid(1988)!=0) sleep(1);
        while(setuid(1988)!=0) sleep(1);
        while(setresuid(1988, 1988, 1988)!=0) sleep(1);

        // child
        // set the limit
        struct rlimit LIM; // time limit, file limit& memory limit
        // time limit
        LIM.rlim_cur = (time_lmt - usedtime / 1000) + 1;
        LIM.rlim_max = LIM.rlim_cur;

        setrlimit(RLIMIT_CPU, &LIM);
        alarm(0);
        alarm(time_lmt*10);
  
        // file limit
        LIM.rlim_max = STD_F_LIM + STD_MB;
        LIM.rlim_cur = STD_F_LIM;
        setrlimit(RLIMIT_FSIZE, &LIM);
        // proc limit
        switch(lang){
	    case 3:  //java
		LIM.rlim_cur=LIM.rlim_max=50;
		break;
	    default:
	      LIM.rlim_cur=LIM.rlim_max=1;
        }
        
	setrlimit(RLIMIT_NPROC, &LIM);

        // set the stack
        LIM.rlim_cur = STD_MB << 6;
        LIM.rlim_max = STD_MB << 6;
        setrlimit(RLIMIT_STACK, &LIM);
        // set the memory
        LIM.rlim_cur = STD_MB *mem_lmt/2*3;
        LIM.rlim_max = STD_MB *mem_lmt*2;
        if(lang<3)
                setrlimit(RLIMIT_AS, &LIM);
        
        switch (lang) {
		case 0:
		case 1:
		case 2:
			//printf("run main...\n");
		        execl("./Main", "./Main", (char *)NULL);
		        break;
		case 3:
		        execl("/usr/bin/java", "/usr/bin/java", java_xms,java_xmx,
		                        "-Djava.security.manager",
		                        "-Djava.security.policy=../etc/java0.policy", "Main", (char *)NULL);
		        break;
        }
        exit(0);
}

void prepare_files(char * filename, int namelen, char * infile, int & p_id,
                char * work_dir, char * outfile, char * userfile, int runner_id) {
        //              printf("ACflg=%d %d check a file!\n",ACflg,solution_id);

        char  fname[BUFFER_SIZE];
        strncpy(fname, filename, namelen);
        fname[namelen] = 0;
        sprintf(infile, "%s/data/%d/%s.in", occm_home, p_id, fname);
        execute_cmd("cp %s %s/data.in", infile, work_dir);
        //execute_cmd("cp %s/data/%d/*.dic %s/", occm_home, p_id,work_dir);

        sprintf(outfile, "%s/data/%d/%s.out", occm_home, p_id, fname);
        sprintf(userfile, "%s/run%d/user.out", occm_home, runner_id);
}

void find_next_nonspace(int & c1, int & c2, FILE *& f1, FILE *& f2, int & ret) {
        // Find the next non-space character or \n.
        while ((isspace(c1)) || (isspace(c2))) {
                if (c1 != c2) {
                        if (c2 == EOF) {
                                do {
                                        c1 = fgetc(f1);
                                } while (isspace(c1));
                                continue;
                        } else if (c1 == EOF) {
                                do {
                                        c2 = fgetc(f2);
                                } while (isspace(c2));
                                continue;
                        } else if ((c1 == '\r' && c2 == '\n')) {
                                c1 = fgetc(f1);
                        } else if ((c2 == '\r' && c1 == '\n')) {
                                c2 = fgetc(f2);
                        } else {
                                if (DEBUG)
                                        printf("%d=%c\t%d=%c", c1, c1, c2, c2);
                                ;
                                ret = OCCM_PE;
                        }
                }
                if (isspace(c1)) {
                        c1 = fgetc(f1);
                }
                if (isspace(c2)) {
                        c2 = fgetc(f2);
                }
        }
}


int get_proc_status(int pid, const char * mark) {
        FILE * pf;
        char fn[BUFFER_SIZE], buf[BUFFER_SIZE];
        int ret = 0;
        sprintf(fn, "/proc/%d/status", pid);
        pf = fopen(fn, "r");
        int m = strlen(mark);
        while (pf && fgets(buf, BUFFER_SIZE - 1, pf)) {

                buf[strlen(buf) - 1] = 0;
                if (strncmp(buf, mark, m) == 0) {
                        sscanf(buf + m + 1, "%d", &ret);
                }
        }
        if (pf)
                fclose(pf);
        return ret;
}

int get_page_fault_mem(struct rusage & ruse, pid_t & pidApp) {
        //java use pagefault
        int m_vmpeak, m_vmdata, m_minflt;
        m_minflt = ruse.ru_minflt * getpagesize();
        if (0 && DEBUG) {
                m_vmpeak = get_proc_status(pidApp, "VmPeak:");
                m_vmdata = get_proc_status(pidApp, "VmData:");
                printf("VmPeak:%d KB VmData:%d KB minflt:%d KB\n", m_vmpeak, m_vmdata,
                                m_minflt >> 10);
        }
        return m_minflt;
}
void print_runtimeerror(char * err){
        FILE *ferr=fopen("error.out","a+");
        fprintf(ferr,"Runtime Error:%s\n",err);
        fclose(ferr);
}

void watch_solution(pid_t pidApp, char * infile, int & ACflg,
                char * userfile, char * outfile, int solution_id, int lang,
                int & topmemory, int mem_lmt, int & usedtime, int time_lmt, int & p_id,
                int & PEflg, char * work_dir) {
        // parent
        int tempmemory;

        if (DEBUG)
                printf("pid=%d judging %s\n", pidApp, infile);

        int status, sig, exitcode;
        struct user_regs_struct reg;
        struct rusage ruse;
        int sub = 0;
        while (1) {
                // check the usage
                wait4(pidApp, &status, 0, &ruse);
                  

//jvm gc ask VM before need,so used kernel page fault times and page size
                if (lang == 3) {
                        tempmemory = get_page_fault_mem(ruse, pidApp);
                } else {//other use VmPeak
                        tempmemory = get_proc_status(pidApp, "VmPeak:") << 10;
                }
		//check memory limit
                if (tempmemory > topmemory)
                        topmemory = tempmemory;
                if (topmemory > mem_lmt * STD_MB) {
                        if (DEBUG)
                                printf("out of memory %d\n", topmemory);
                        if (ACflg == OCCM_AC)
                                ACflg = OCCM_ML;
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                        break;
                }
                  //sig = status >> 8;

                if (WIFEXITED(status))
                        break;
                if (lang < 4  && get_file_size("error.out")) {
                        ACflg = OCCM_RE;
                        addreinfo(solution_id);
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                        break;
                }
		
		//check output limit exceeded
                if (get_file_size(userfile) > get_file_size(outfile) * 2+1024) {
                        ACflg = OCCM_OL;
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                        break;
                }

                exitcode = WEXITSTATUS(status);
                /*exitcode == 5 waiting for next CPU allocation          
                 *  */
                if ((lang >= 3 && exitcode == 17) || exitcode == 0x05 || exitcode == 0)
                        //go on and on
                        ;
                else {

                        if (DEBUG) {
                                printf("status>>8=%d\n", exitcode);

                        }
                        //psignal(exitcode, NULL);

                        if (ACflg == OCCM_AC){
                                switch (exitcode) {
                                        case SIGCHLD:
                                        case SIGALRM:
               				alarm(0);
                                        case SIGKILL:
                                        case SIGXCPU:
                                                ACflg = OCCM_TL;
                                                break;
                                        case SIGXFSZ:
                                                ACflg = OCCM_OL;
                                                break;
                                        default:
                                                ACflg = OCCM_RE;
                                }
                                print_runtimeerror(strsignal(exitcode));
                        }
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);

                        break;
                }
                if (WIFSIGNALED(status)) {
                        /*  WIFSIGNALED: if the process is terminated by signal
                         *
                         *  psignal(int sig, char *s)，like perror(char *s)，print out s, with error msg from system of sig  
       * sig = 5 means Trace/breakpoint trap
       * sig = 11 means Segmentation fault
       * sig = 25 means File size limit exceeded
       */
                        sig = WTERMSIG(status);

                        if (DEBUG) {
                                printf("WTERMSIG=%d\n", sig);
                                psignal(sig, NULL);
                        }
                        if (ACflg == OCCM_AC){
                                switch (sig) {
                                case SIGCHLD:
                                case SIGALRM:
             				alarm(0);
                                case SIGKILL:
                                case SIGXCPU:
                                        ACflg = OCCM_TL;
                                        break;
                                case SIGXFSZ:
                                        ACflg = OCCM_OL;
                                        break;

                                default:
                                        ACflg = OCCM_RE;
                                }
                                print_runtimeerror(strsignal(sig));
                        }
                        break;
                }
                /*     

  WIFSTOPPED: return true if the process is paused or stopped while ptrace is watching on it
  WSTOPSIG: get the signal if it was stopped by signal
                 */

                // check the system calls
                ptrace(PTRACE_GETREGS, pidApp, NULL, &reg);

                if (!record_call&&call_counter[reg.REG_SYSCALL] == 0) { //do not limit JVM syscall for using different JVM
                        ACflg = OCCM_RE;
                        char error[BUFFER_SIZE];
                        sprintf(error,"[ERROR] A Not allowed system call: runid:%d callid:%ld\n",
                                        solution_id, reg.REG_SYSCALL);
                        write_log(error);
                        print_runtimeerror(error);
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                }else if(record_call){
			call_counter[reg.REG_SYSCALL]=1;
                } else {
                        if (sub == 1 && call_counter[reg.REG_SYSCALL] > 0)
                                call_counter[reg.REG_SYSCALL]--;
                }
                sub = 1 - sub;

                
                ptrace(PTRACE_SYSCALL, pidApp, NULL, NULL);
        }
        usedtime += (ruse.ru_utime.tv_sec * 1000 + ruse.ru_utime.tv_usec / 1000);
        usedtime += (ruse.ru_stime.tv_sec * 1000 + ruse.ru_stime.tv_usec / 1000);
  	usedtime = usedtime?usedtime:1;
        //clean_session(pidApp);
}

const char * getFileNameFromPath(const char * path){
   for(int i=strlen(path);i>=0;i--){
        if(path[i]=='/')
                return &path[i];
   }
   return path;
}

void make_diff_out(FILE *f1,FILE *f2,int c1,int c2,const char * path){
   FILE *out;
   char buf[45];
   out=fopen("diff.out","a+");
   fprintf(out,"=================%s\n",getFileNameFromPath(path));
   fprintf(out,"Right:\n%c",c1);
   if(fgets(buf,44,f1)){
        fprintf(out,"%s",buf);
   }
   fprintf(out,"\n-----------------\n");
   fprintf(out,"Your:\n%c",c2);
   if(fgets(buf,44,f2)){
        fprintf(out,"%s",buf);
   }
   fprintf(out,"\n=================\n");
   fclose(out);
}

int compare_zoj(const char *file1, const char *file2) {
        int ret = OCCM_AC;
	int c1,c2;
        FILE * f1, *f2 ;
        f1 = fopen(file1, "r");
        f2 = fopen(file2, "r");
        if (!f1 || !f2) {
                ret = OCCM_RE;
        } else
                for (;;) {
                        // Find the first non-space character at the beginning of line.
                        // Blank lines are skipped.
                        c1 = fgetc(f1);
                        c2 = fgetc(f2);
                        find_next_nonspace(c1, c2, f1, f2, ret);
                        // Compare the current line.
                        for (;;) {
                                // Read until 2 files return a space or 0 together.
                                while ((!isspace(c1) && c1) || (!isspace(c2) && c2)) {
                                        if (c1 == EOF && c2 == EOF) {
                                                goto end;
                                        }
                                        if (c1 == EOF || c2 == EOF) {
                                                break;
                                        }
                                        if (c1 != c2) {
                                                // Consecutive non-space characters should be all exactly the same
                                                ret = OCCM_WA;
                                                goto end;
                                        }
                                        c1 = fgetc(f1);
                                        c2 = fgetc(f2);
                                }
                                find_next_nonspace(c1, c2, f1, f2, ret);
                                if (c1 == EOF && c2 == EOF) {
                                        goto end;
                                }
                                if (c1 == EOF || c2 == EOF) {
                                        ret = OCCM_WA;
                                        goto end;
                                }

                                if ((c1 == '\n' || !c1) && (c2 == '\n' || !c2)) {
                                        break;
                                }
                        }
                }
        end: 
       if(ret==OCCM_WA)make_diff_out(f1,f2,c1,c2,file1);
	if (f1)
                fclose(f1);
        if (f2)
                fclose(f2);
        return ret;
}

void delnextline(char s[]) {
        int L;
        L = strlen(s);
        while (L > 0 && (s[L - 1] == '\n' || s[L - 1] == '\r'))
                s[--L] = 0;
}

int compare(const char *file1, const char *file2) {
        //compare ported and improved from zoj don't limit file size
        return compare_zoj(file1, file2);
}

int fix_java_mis_judge(char *work_dir, int & ACflg, int & topmemory,
                int mem_lmt) {
        int comp_res = OCCM_AC;
        if (DEBUG)
                execute_cmd("cat %s/error.out", work_dir);
        comp_res = execute_cmd("grep 'Exception'  %s/error.out", work_dir);
        if (!comp_res) {
                printf("Exception reported\n");
                ACflg = OCCM_RE;
        }        
                
        comp_res = execute_cmd("grep 'java.lang.OutOfMemoryError'  %s/error.out",
                        work_dir);

        if (!comp_res) {
                printf("JVM need more Memory!");
                ACflg = OCCM_ML;
                topmemory = mem_lmt * STD_MB;
        }
        comp_res = execute_cmd("grep 'java.lang.OutOfMemoryError'  %s/user.out",
                        work_dir);

        if (!comp_res) {
                printf("JVM need more Memory or Threads!");
                ACflg = OCCM_ML;
                topmemory = mem_lmt * STD_MB;
        }
        comp_res = execute_cmd("grep 'Could not create'  %s/error.out", work_dir);
        if (!comp_res) {
                printf("jvm need more resource,tweak -Xmx(OCCM_JAVA_BONUS) Settings");
                ACflg = OCCM_RE;
                //topmemory=0;
        }
        return comp_res;
}

void judge_solution(int & ACflg, int & usedtime, int time_lmt,
                int p_id, char * infile, char * outfile, char * userfile, int & PEflg,
                int lang, char * work_dir, int & topmemory, int mem_lmt, int solution_id ,double num_of_test)  {
        //usedtime-=1000;
        int comp_res;
        num_of_test=1.0;
        if (ACflg == OCCM_AC && usedtime > time_lmt * 1000*(use_max_time?1:num_of_test))
                ACflg = OCCM_TL;
        if(topmemory>mem_lmt * STD_MB) ACflg=OCCM_ML; //issues79
        // compare
        if (ACflg == OCCM_AC) {
                comp_res = compare(outfile, userfile);
		if (comp_res == OCCM_WA) {
                        ACflg = OCCM_WA;
                        if (DEBUG)
                                printf("fail test %s\n", infile);
                } else if (comp_res == OCCM_PE)
                        PEflg = OCCM_PE;
                ACflg = comp_res;
        }
        //jvm popup messages, if don't consider them will get miss-WrongAnswer
        if (lang == 3) {
                comp_res = fix_java_mis_judge(work_dir, ACflg, topmemory, mem_lmt);
        }
}

void adddiffinfo(int solution_id) {
	_addreinfo_mysql(solution_id,"diff.out");
}

void print_call_array(){
	printf("int LANG_%sV[256]={",LANG_NAME);
	int i=0;
	for (i = 0; i<call_array_size; i++){
         if(call_counter[i]){		 
	          printf("%d,",i);	 
	     }
	}
	printf("0};\n");
	
	printf("int LANG_%sC[256]={",LANG_NAME);
	for (i = 0; i<call_array_size; i++){
         if(call_counter[i]){		 
	          printf("OCCM_MAX_LIMIT,");	 
	     }
	}
	printf("0};\n");
}

int main(int argc, char** argv) {

	char work_dir[BUFFER_SIZE];
	char user_id[BUFFER_SIZE];
	int istest=0;
	int solution_id = 1000;
	int runner_id = 0; //client id
	int p_id, time_lmt, mem_lmt, lang, max_case_time=0;

	init_parameters(argc, argv, solution_id, runner_id, istest);
	if(istest)
	{
		printf("test mode\n");
		//return 0;
	}
	strcpy(occm_home,"/home/occm");
	init_mysql_conf();
	
	if (!init_mysql_conn()) {
		exit(0); //exit if mysql is down
	}
        //set work directory to start running & judging
	sprintf(work_dir, "%s/run%s/", occm_home, argv[2]);
        
	chdir(work_dir);
        if (!DEBUG)
		clean_workdir(work_dir);
	get_solution_info(solution_id, p_id, user_id, lang);
	
	//get the limit
	if(p_id==0){
		time_lmt=5;
		mem_lmt=128;
	}else{
		get_problem_info(p_id, time_lmt, mem_lmt);
	}
	

	//copy source file
        get_solution(solution_id, work_dir, lang);
	
        //java is lucky
	if (lang >= 3) {
		// the limit for java
		time_lmt = time_lmt + java_time_bonus;
		mem_lmt = mem_lmt + java_memory_bonus;
		// copy java.policy
		execute_cmd( "cp %s/etc/java0.policy %sjava.policy", occm_home, work_dir);
	}

        // set range of limit
        if (time_lmt > 300 || time_lmt < 1)
                time_lmt = 300;
        if (mem_lmt > 1024 || mem_lmt < 1)
                mem_lmt = 1024;

        if (DEBUG)
                printf("time: %d mem: %d\n", time_lmt, mem_lmt);

        // set the result to compiling
        int Compile_OK;

        Compile_OK = compile(lang);
	printf("Compile: %d\n", Compile_OK);
	
        if (Compile_OK != 0) {
                addceinfo(solution_id);
                update_solution(solution_id, OCCM_CE, 0, 0);
                mysql_close(conn);
                if (!DEBUG)
                        clean_workdir(work_dir);
                else
                        write_log("compile error");
                exit(0);
        } else {
                update_solution(solution_id, OCCM_RI, 0, 0);
        }
	
        // run
        char fullpath[BUFFER_SIZE];
        char infile[BUFFER_SIZE];
        char outfile[BUFFER_SIZE];
        char userfile[BUFFER_SIZE];
        sprintf(fullpath, "%s/data/%d", occm_home, p_id); // the fullpath of data dir
	// open DIRs
        DIR *dp;
        dirent *dirp;
	
        if (istest==0&&(dp = opendir(fullpath)) == NULL) {
                write_log("No such dir:%s!\n", fullpath);
                mysql_close(conn);
                exit(-1);
        }
        int ACflg, PEflg;
        ACflg = PEflg = OCCM_AC;
        int namelen;
        int usedtime = 0, topmemory = 0;

        // read files and run
        int num_of_test=1;
	//test mode
	if(istest){  //custom input running
		printf("test_mode\n");
		get_custominput(solution_id,work_dir);
		printf("workdir: %s\n", work_dir);
		init_syscalls_limits(lang);
		pid_t pidApp = fork();

		if (pidApp == 0) {
			run_solution(lang, work_dir, time_lmt, usedtime, mem_lmt);
		} else {
			watch_solution(pidApp, infile, ACflg, userfile, outfile, solution_id, lang, topmemory, mem_lmt, usedtime, time_lmt, p_id, PEflg, work_dir);
		}

		if(ACflg == OCCM_TL){
			usedtime=time_lmt*1000;
		}
		if(ACflg==OCCM_RE){
			if(DEBUG) printf("add RE info of %d..... \n",solution_id);
			addreinfo(solution_id);
		}else{   
			addcustomout(solution_id, "user.out");
		}
		update_solution(solution_id, OCCM_TR, usedtime, topmemory >> 10);
		exit(0);
	}

	for (;(oi_mode|| ACflg == OCCM_AC )&& (dirp = readdir(dp)) != NULL;) {
		namelen = isInFile(dirp->d_name); // check if the file is *.in or not	
		if (namelen == 0)
		        continue;
		prepare_files(dirp->d_name, namelen, infile, p_id, work_dir, outfile, userfile, runner_id);
		init_syscalls_limits(lang);
		
		pid_t pidApp = fork();

		if (pidApp == 0) {
			printf("Start run solution\n");
		        run_solution(lang, work_dir, time_lmt, usedtime, mem_lmt);
		} else {		        
			num_of_test++;

			watch_solution(pidApp, infile, ACflg, userfile, outfile,
				solution_id, lang, topmemory, mem_lmt, usedtime, time_lmt,
				p_id, PEflg, work_dir);
			judge_solution(ACflg, usedtime, time_lmt, p_id, infile,
						outfile, userfile, PEflg, lang, work_dir, topmemory,
						mem_lmt, solution_id,num_of_test);
		        if(use_max_time){				
		                max_case_time=usedtime>max_case_time?usedtime:max_case_time;
				printf("max_case_time: %d\n", max_case_time);
		                usedtime=0;
		        }
		}
	}
	if (ACflg == OCCM_AC && PEflg == OCCM_PE)
	{
		printf("ACflg = OCCM_PE;\n");
		ACflg = OCCM_PE;
	}
	
	if(ACflg==OCCM_RE){
		if(DEBUG) 
			printf("add RE info of %d..... \n",solution_id);
		addreinfo(solution_id);
	}

	if(use_max_time){
		usedtime=max_case_time;
	}

	if(ACflg == OCCM_TL){
		usedtime=time_lmt*1000;
	}
	update_solution(solution_id, ACflg, usedtime, topmemory >> 10);

        clean_workdir(work_dir);

        if (DEBUG)
                write_log("result=%d", ACflg);
        mysql_close(conn);

	if(record_call){
		print_call_array();		
	}
        closedir(dp);
        return 0;
}
