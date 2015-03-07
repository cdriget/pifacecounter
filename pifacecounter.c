/******************************************************************************/
/*** Program      : pifacecounter.c                                         ***/
/***                                                                        ***/
/*** Author       : Christophe DRIGET                                       ***/
/***                                                                        ***/
/*** Creation     : Janvier 2015                                            ***/
/*** Modification :                                                         ***/
/***                                                                        ***/
/*** Version      : 1.0                                                     ***/
/***                                                                        ***/
/*** Description  : Count PiFace Inputs and insert values into MySQL        ***/
/***                                                                        ***/
/*** Compilation  : gcc -o pifacecounter pifacecounter.c -Ilibpifacedigital/src/ -Llibpifacedigital -lpifacedigital -Llibmcp23s17/ -lmcp23s17 -lpthread -lmysqlclient -lconfig -lrt  ***/
/******************************************************************************/

///*** Header files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <unistd.h>
#include <libconfig.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <syslog.h>
#include <mysql/mysql.h>
#include "libpifacedigital/src/pifacedigital.h"


#define HW_ADDR 0            // PiFaceDigital hardware address
#define INTERVAL 60          // 60 seconds
#define NAME "pifacecounter" // Program name used for configuration file and syslog messages
//#define NAME "test" // Program name used for configuration file and syslog messages


///*** Global Variables
volatile sig_atomic_t keep_going = 1;  // Flag controlling termination of main loop
uint8_t base_inputs, last_inputs;
int devices[8];
int values[8];


/***
 *** Signal handler
 ***/
void signal_handler (int sig)
{
	if (sig == SIGINT) {             // Break / Ctrl+C
		printf("Received SIGINT\n");
		syslog(LOG_WARNING, "Received SIGINT");
	}
	else if (sig == SIGTERM) {       // kill -15 (default)
		printf("Received SIGTERM\n");
		syslog(LOG_WARNING, "Received SIGTERM");
	}

	// Clear flag
	keep_going = 0;

	// Re-enables itself
	signal (sig, signal_handler);
}


/***
 *** Error function
 ***/
void error_exit(int error_code, char *message)
{
	fprintf(stderr, "%s\n", message);
	syslog(LOG_WARNING, message);
	exit(error_code);
}


/***
 *** Interrupt listening thread
 ***/
void *listening_thread(void *arg)
{
	int i;
	uint8_t inputs; // Input bits (pins 0-7)
	uint8_t diff;
	int ret;        // Interrupt return value
	//char bits[9];

	printf("Listening thread successfully started. Waiting for input...\n");
	syslog(LOG_INFO, "Listening thread successfully started. Waiting for input...");

	//bits[8] = 0;

	while (keep_going) {
		/**
		 * Wait for input change interrupt
		 */
		ret = pifacedigital_wait_for_input(&inputs, -1, HW_ADDR);
		if (ret > 0) {
			//printf("Inputs: 0x%x => ", inputs);
			// Test if a bit has changed
			if ( diff = inputs ^ base_inputs ) {
				//for (i=7; i>=0; i--) {
				for (i=0; i<8; i++) {
					//printf("%c", (inputs&(1<<i))?'1':'0');
					//bits[i] = (inputs&(1<<i))?'1':'0';
					if (diff&(1<<i)) {
						if ( (inputs&(1<<i)) != (last_inputs&(1<<i)) ) {
							printf("Pin : %d\n", i);
							if (devices[i])
								values[i]++;
						}
					}
				}
				//printf("%s\n", bits);
			}
			last_inputs = inputs;
		}
		else if (ret == 0) {
			perror("Interrupt timeout !\n");
			syslog(LOG_WARNING, "Interrupt timeout");
		}
		else {
			perror("Interrupt error !\n");
			syslog(LOG_WARNING, "Interrupt error");
			keep_going = 0;
			break;
		}
	}

	printf("Exiting thread.\n");
	/* Pour enlever le warning */
	//(void) arg;
	pthread_exit(NULL);
}


/***
 *** Main Loop
 ***/
int main(int argc, char *argv[])
{
	/**
	 * Variables definition
	 */
	// General
	unsigned int count = 0;
	int i;
	int value;
	// File
	FILE *fp;
	// Threads
	pthread_t thread1;
	// MySQL
	MYSQL mysql;
	char query[255];
	// Date & Time
	//struct timeval tv;
	struct timespec tp;
	time_t curtime;
	struct tm *loctime;
	double start_time, last_time, now, next_time;
	//double delta_time;
	unsigned sleep_time;


	/**
	 * Variable initialization
	 */
	for (i=0; i<8; i++) {
		devices[i] = 0;
		values[i] = 0;
	}


	/**
	 * Open Syslog connection
	 */
	openlog (NAME, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);


	/**
	 * Create signals handler
	 */
	printf("Creating signals handler... ");
	if (signal(SIGINT, signal_handler) == SIG_ERR) {
		//perror("Error : can't create SIGINT signal handler !\n");
		//return EXIT_FAILURE;
		error_exit(EXIT_FAILURE, "Error : can't create SIGINT signal handler !");
	}
	if (signal(SIGTERM, signal_handler) == SIG_ERR) {
		error_exit(EXIT_FAILURE, "Error : can't create SIGTERM signal handler !");
	}
	printf("OK\n");


	/**
	 * Read configuration file
	 */

	config_t cfg, *cf;
	char config_file[64];
	const char *config_mysql_host, *config_mysql_user, *config_mysql_password, *config_mysql_db;
	const config_setting_t *config_counters_list, *config_counter;
	int config_counters_count, config_counter_id;
	//const char *config_counter_name = NULL;

	//*** Initialize configuration object
	cf = &cfg;
	config_init(cf);

	//*** Parse configuration file 
	sprintf(config_file, "/home/pi/%s.conf", NAME);
	printf("Opening configuration file : %s\n", config_file);
	if (config_read_file(cf, config_file)) {

		//*** Read PiFace Inputs parameters
		if ( config_counters_list = config_lookup(cf, "piface.counters") ) {
			if ( config_counters_count = config_setting_length(config_counters_list) ) {
				//printf("Found %d counters :\n", config_counters_count);
				for (i = 0; i < config_counters_count; i++) {
					//printf("\t#%d. %d\n", i + 1, config_setting_get_int_elem(config_counters_list, i));
					config_counter = config_setting_get_elem (config_counters_list, i);
					if (config_setting_lookup_int(config_counter, "input", &config_counter_id)) {
						//printf("\t#%d. %d\n", i + 1, config_counter_id);
						if (config_counter_id < 8) {
							printf("Found device : %d\n", config_counter_id);
							devices[config_counter_id] = 1;
						}
					}
					//if (config_setting_lookup_string(config_counter, "name", &config_counter_name))
						//printf("\t#%d. %s\n", i + 1, config_counter_name);
				}
			}
			else {
				config_destroy(cf);
				error_exit(EXIT_FAILURE, "Error : No PiFace counter found in configuration file !");
			}
		}
		else {
			config_destroy(cf);
			error_exit(EXIT_FAILURE, "Error : PiFace counters not found in configuration file !");
		}

		//*** Read MySQL parameters
		if (config_lookup_string(cf, "mysql.host", &config_mysql_host)) {
			char *p_config_mysql_host;
			if ( p_config_mysql_host = malloc(sizeof(char)*(strlen(config_mysql_host)+1)) ) {
				strcpy(p_config_mysql_host, config_mysql_host);
				config_mysql_host = p_config_mysql_host;
			}
			else
				error_exit(EXIT_FAILURE, "Error : malloc() failed !");
		}
		else {
			config_destroy(cf);
			error_exit(EXIT_FAILURE, "Error : MySQL host not found in configuration file !");
		}
		if (config_lookup_string(cf, "mysql.user", &config_mysql_user)) {
			char *p_config_mysql_user;
			if ( p_config_mysql_user = malloc(sizeof(char)*(strlen(config_mysql_user)+1)) ) {
				strcpy(p_config_mysql_user, config_mysql_user);
				config_mysql_user = p_config_mysql_user;
			}
			else
				error_exit(EXIT_FAILURE, "Error : malloc() failed !");
		}
		else {
			config_destroy(cf);
			error_exit(EXIT_FAILURE, "Error : MySQL user not found in configuration file !");
		}
		if (config_lookup_string(cf, "mysql.password", &config_mysql_password)) {
			char *p_config_mysql_password;
			if ( p_config_mysql_password = malloc(sizeof(char)*(strlen(config_mysql_password)+1)) ) {
				strcpy(p_config_mysql_password, config_mysql_password);
				config_mysql_password = p_config_mysql_password;
			}
			else
				error_exit(EXIT_FAILURE, "Error : malloc() failed !");
		}
		else {
			config_destroy(cf);
			error_exit(EXIT_FAILURE, "Error : MySQL password not found in configuration file !");
		}
		if (config_lookup_string(cf, "mysql.db", &config_mysql_db)) {
			char *p_config_mysql_db;
			if ( p_config_mysql_db = malloc(sizeof(char)*(strlen(config_mysql_db)+1)) ) {
				strcpy(p_config_mysql_db, config_mysql_db);
				config_mysql_db = p_config_mysql_db;
			}
			else
				error_exit(EXIT_FAILURE, "Error : malloc() failed !");
		}
		else {
			config_destroy(cf);
			error_exit(EXIT_FAILURE, "Error : MySQL db not found in configuration file !");
		}

		//*** Cleanup
		config_destroy(cf);
	}
	else {
		fprintf(stderr, "%s:%d - %s\n", config_error_file(cf), config_error_line(cf), config_error_text(cf));
		config_destroy(cf);
		return EXIT_FAILURE;
	}


	/**
	 * Read configuration file
	 */
	/*if ( fp = fopen("/home/pi/pifacecounter.conf", "r") ) {
		char buffer[256];
		char *line, *record, *endptr;
		while ( (line=fgets(buffer,sizeof(buffer),fp)) != NULL )	{
			//printf("%s", line);
			record = (char *)strtok(line, ";");
			//while (record != NULL) {
			if (record != NULL) {
				int ret = strtol(record, &endptr, 10);
				if (!*endptr) {
					printf("Found device : %d\n", ret);
					devices[ret] = 1;
				}
				//record = (char *)strtok(NULL, ";");
			}
		}
		fclose(fp);
	}
	else {
		error_exit(EXIT_FAILURE, "Error : can't open configuration file !");
	}*/


  /**
	 * Initialize MySQL object
	 */
	if ( mysql_init(&mysql) == NULL )
		error_exit(EXIT_FAILURE, "Error : can't initialize MySQL object !");
	if ( mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "option") )
		error_exit(EXIT_FAILURE, "Error : can't set MySQL options !");


	/**
	 * Open PiFace Digital SPI connection
	 */
	printf("Opening PiFace Digital connection at location %d... ", HW_ADDR);
	if ( ! pifacedigital_open(HW_ADDR) )
		error_exit(EXIT_FAILURE, "Error : can't open connection to PiFace !");
	printf("OK\n");


	/**
	 * Read initial inputs
	 */
	base_inputs = pifacedigital_read_reg(INPUT, HW_ADDR);



	//for (i=7; i>=0; i--)
		//printf("%c", (base_inputs&(1<<i))?'1':'0');
	//printf("\n");

	//return 0;




	/**
	 * Enable interrupts processing (only required for all blocking/interrupt methods)
	 */
	printf("Enable interrupts processing... ");
	if ( pifacedigital_enable_interrupts() )
		error_exit(EXIT_FAILURE, "Error : can't enable interrupts. Try running using sudo to enable PiFaceDigital interrupts.");
	printf("OK\n");


	/**
	 * Create listening thread
	 */
	printf("Creating thread... ");
	if ( pthread_create(&thread1, NULL, listening_thread, NULL) == -1 )
		error_exit(EXIT_FAILURE, "Error : can't create thread !");
	printf("OK\n");


	/**
	 * Get current time
	 */
	if ( ! clock_gettime(CLOCK_MONOTONIC, &tp) )
		start_time = (double) tp.tv_sec + (tp.tv_nsec / 1000000000.0);
	else
		error_exit(EXIT_FAILURE, "Error : can't get current time !");


	/**
	 * Main loop
	 */
	syslog (LOG_INFO, "Main loop started");
	while (keep_going) {

		count++;

		//*** Get current time
		if ( ! clock_gettime(CLOCK_MONOTONIC, &tp) ) {
			//printf("start_time = %f\n", start_time);
			//printf("INTERVAL = %d\n", INTERVAL);
			//printf("count = %d\n", count);
			//printf("tv.tv_sec = %f\n", (double)tv.tv_sec);
			//printf("tv.tv_usec / 1000000.0 = %f\n", tv.tv_usec / 1000000.0);
			sleep_time = (unsigned int) (start_time + (double)(INTERVAL * count) - (double)(tp.tv_sec) - (tp.tv_nsec / 1000000000.0) );
		}
		else
			sleep_time = INTERVAL;
		printf("Sleeping %d seconds...\n", sleep_time);
		sleep(sleep_time);

		//if ( mysql_real_connect(&mysql, "localhost", "domotique", "password", "domotique", 0, NULL, 0) ) {
		if ( mysql_real_connect(&mysql, config_mysql_host, config_mysql_user, config_mysql_password, config_mysql_db, 0, NULL, 0) ) {
			for (i=0; i<8; i++) {
				if (devices[i]) {
					value = values[i];
					values[i] = 0;
					printf("Counter %d = %d\n", i, value);
					sprintf(query, "INSERT INTO counter_value (counter_id, value, cumul) SELECT %d, %d, COALESCE(MAX(cumul),0)+%d FROM counter_value WHERE counter_id=%d;", i, value, value, i);
					if ( mysql_query(&mysql, query) ) {
						printf("%s\n", query);
						perror("Error : can't insert data into MySQL Database !\n");
						syslog(LOG_WARNING, "Error : can't insert data into MySQL Database !");
						values[i] += value;
					}
				}
			}
			mysql_close(&mysql);
		}
		else {
			perror("Error : can't connect to MySQL Database !\n");
			syslog(LOG_WARNING, "Error : can't connect to MySQL Database !");
		}
	}

	/**
	 * Disable interrupts
	 */
	printf("Disable interrupts... ");
	if ( pifacedigital_disable_interrupts() ) {
		perror("Error : can't disable interrupts !\n");
		syslog(LOG_WARNING, "Error : can't disable interrupts !");
	}
	else
		printf("OK\n");

	/**
	 * Close PiFace Digital connection
	 */
	pifacedigital_close(HW_ADDR);

	/**
	 * Clean exit
	 */
	free((char *)config_mysql_host);
	free((char *)config_mysql_user);
	free((char *)config_mysql_password);
	free((char *)config_mysql_db);
	syslog (LOG_INFO, "Clean exit");
	closelog ();
	return EXIT_SUCCESS;
}
