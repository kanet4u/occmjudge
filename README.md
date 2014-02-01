occmjudge
=========
<pre>
Code compiling and testing system


OCCM server install

1. Create user:
	useradd --uid 1988 occm

2. Create dirs:
	mkdir /home/occm/
	mkdir /home/occm/etc
	mkdir /home/occm/data
	mkdir /home/occm/log
	mkdir /home/occm/run0

3. Create or copy occm.conf file to /home/occm/etc/
	OCCM_HOST_NAME=127.0.0.1
	OCCM_USER_NAME=root
	OCCM_PASSWORD=kanet./1988
	OCCM_DB_NAME=occm
	OCCM_PORT_NUMBER=3306
	OCCM_RUNNING=4
	OCCM_SLEEP_TIME=5
	OCCM_JAVA_TIME_BONUS=2
	OCCM_JAVA_MEMORY_BONUS=512
	OCCM_JAVA_XMS=-Xms32m
	OCCM_JAVA_XMX=-Xmx256m
	OCCM_USE_MAX_TIME=1
	OCCM_LANG_SET=0,1,2,3

4. Create or copy java0.policy file to /home/occm/etc/
	grant {
	    permission java.io.FilePermission "./*", "read";
	    permission java.io.FilePermission "./*", "write";
	};

5. ...
</pre>
