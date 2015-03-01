#include <string>
#include <vector>
#include <iostream>
#include <boost/thread.hpp>             // for threads
#include <boost/program_options.hpp>            // for cmdline args parsing
#include <unistd.h>                                                     // for sleep()
#include <math.h>                                                       // for floor()
#include <oping.h>                                                      // for ping()
#include <libconfig.h>                                                  // for config parsing
#include <sys/wait.h>                                                   // for waitpid()
#include <sys/prctl.h>                                                  // for childrens dying after parent die
#include <sys/stat.h>                                                   // for umask()
#include <syslog.h>                                                     // for logging
#include <occi.h>                                                       // for oracle

namespace po = boost::program_options;          // for better usage
using namespace std;
using namespace oracle::occi;

// for DB locks:
boost::mutex mtx_;
Environment *env;
Connection *ora;

// Switch struct
typedef struct {
	Number id;
	std::string ip;
	bool alive;
	Number value_id;
} Switch;

// for memory usage
typedef struct {
	unsigned long size,resident,share,text,lib,data,dt;
} statm_t;

int do_work( vector< Switch > s);
bool ping( std::string host );
void read_mem( statm_t &result );
int workers;
bool dmn;
long long int switch_root_id;
long long int alive_param_id;

// db props
const char *dbhost, *dbuser, *dbpassword, *dbsid;


void sigHandler( int signo ) {
	if(signo == SIGINT) {
		if( !dmn )
			printf("SIGINT: Aborting.\n");
		syslog(LOG_INFO, "SIGINT: Aborting.\n");
		exit(1);
	}
}

int main( int argc, char *argv[]) {
	env;
	ora = NULL;
	dmn = false;
	
	int pid1 = getpid();
	dmn = false;
	openlog("hpinger", LOG_PID, LOG_DAEMON);
	
	// Check for the root privs, required for ping
	if( getuid() != 0 ) {
		fprintf( stderr, "%s: root privs required.\n", *(argv +0));
		exit(1);
	}
	
	signal(SIGINT, sigHandler);
	
	// Parse cmdargs
	po::options_description desc("Available options");
	desc.add_options()
		("help,h", "display this message")
		("daemonize,d", "run program as daemon")
		("workers,w", po::value<int>()->default_value(32), "workers count")
		("config,c", po::value<std::string>()->default_value("/etc/hpinger.conf"), "config file location")
		("pid,p", po::value<std::string>()->default_value("/var/run/hpinger.pid"), "pid file location")
	;
	po::variables_map vm;
	po::store(po::parse_command_line(argc,argv,desc), vm);
	po::notify(vm);
	
	// Display help
	if( vm.count("help") ) {
		cout << desc << endl;
		exit(1);
	}
	
	workers = vm["workers"].as<int>();
	
	config_t cfg;
	config_setting_t *settings;
	config_init( &cfg );
	
	if( !config_read_file( &cfg, vm["config"].as<std::string>().c_str() )) {
		fprintf(stderr, "Can't open config file: %s\n", vm["config"].as<std::string>().c_str());
		syslog(LOG_ERR, "Can't open config file: %s\n", vm["config"].as<std::string>().c_str());
		config_destroy(&cfg);
		exit(1);
	}
	
	if( !config_lookup_string(&cfg, "dbhost", &dbhost)){
		fprintf(stderr, "Can't read 'dbhost' value in config.\n");
		syslog(LOG_ERR, "Can't read 'dbhost' value in config.\n");
		exit(1);
	}
	if( !config_lookup_string(&cfg, "dbuser", &dbuser)){
		fprintf(stderr, "Can't read 'dbuser' value in config.\n");
		syslog(LOG_ERR, "Can't read 'dbuser' value in config.\n");
		exit(1);
	}
	if( !config_lookup_string(&cfg, "dbpassword", &dbpassword)){
		fprintf(stderr, "Can't read 'dbpassword' value in config.\n");
		syslog(LOG_ERR, "Can't read 'dbpassword' value in config.\n");
		exit(1);
	}
	if( !config_lookup_string(&cfg, "dbsid", &dbsid)){
		fprintf(stderr, "Can't read 'dbsid' value in config.\n");
		syslog(LOG_ERR, "Can't read 'dbsid' value in config.\n");
		exit(1);
	}
	if( !config_lookup_int64(&cfg, "switch_root_id", &switch_root_id)) {
		fprintf(stderr, "Can't read 'switch_root_id' from config.\n");
		syslog(LOG_ERR, "Can't read 'switch_root_id' from config.\n");
		exit(1);
	}
	if( !config_lookup_int64(&cfg, "alive_param_id", &alive_param_id)) {
		fprintf(stderr, "Can't read 'alive_param_id' from config.\n");
		syslog(LOG_ERR, "Can't read 'alive_param_id' from config.\n");
		exit(1);
	}
	
	// daemonizing
	if( vm.count("daemonize") ) {
		dmn = true;
		int mpid = fork();
		if( mpid < 0 ) {
			fprintf( stderr, "Fork failed!\n");
			syslog( LOG_ERR, "Fork failed!\n");
			exit(-1);
		}
		syslog( LOG_INFO, "hpinger started as daemon; pid: %i\n", getpid());
		if( mpid > 0 ) {
			// this is parent
			exit(1);
		}
		
		pid1 = getpid();
		umask(0);
		int sid = setsid();
		if( sid < 0 ) {
			fprintf( stderr, "setsid() failed!\n");
			syslog( LOG_ERR, "setsid() failed!\n");
			exit(-1);
		}
		
		pid_t pid = getpid();
		FILE *fp = fopen( vm["pid"].as<std::string>().c_str(), "w");
		if( fp ) {
			fprintf(fp, "%d\n", pid);
		}
		fclose(fp);
	}
	
	// Oracle connection
	env = Environment::createEnvironment(Environment::DEFAULT);
	while( true )
	{
		// print memory usage to log file
		statm_t mem;
		read_mem(mem);
		if( !dmn )
			printf("Memory usage: %ld\n", mem.size);
		syslog(LOG_INFO, "Memory usage: %ld\n", mem.size);
		
		try {
			ora = env->createConnection(dbuser, dbpassword, dbsid);
		} catch (SQLException &e ) {
			syslog(LOG_ERR, "Oracle connection error: %s.\n", e.getMessage().c_str() );
			if( !dmn )
				fprintf(stderr, "Oracle connection error: %s.\n", e.getMessage().c_str() );
			sleep(60);
			continue;
		}
		
		ResultSet* ores;
		Statement* sth;
		int switchCount = 0;
		
		// Get count of switches
		try {
			sth = ora->createStatement("\
			SELECT COUNT(IPADR.VC_VISUAL_CODE) \
			FROM SI_V_OBJECTS_SIMPLE O \
			INNER JOIN SR_V_GOODS_SIMPLE G \
				ON G.N_GOOD_TYPE_ID=1 AND G.N_GOOD_ID=O.N_GOOD_ID \
			INNER jOIN SR_V_GOODS_SIMPLE G2 \
				ON G2.N_GOOD_ID=G.N_PARENT_GOOD_ID \
			INNER JOIN SI_V_OBJECTS_SPEC_SIMPLE OSPEC \
				ON OSPEC.N_MAIN_OBJECT_ID=O.N_OBJECT_ID AND OSPEC.VC_NAME LIKE 'CPU %' \
			INNER JOIN SI_V_OBJ_ADDRESSES_SIMPLE_CUR IPADR \
				ON IPADR.N_ADDR_TYPE_ID=SYS_CONTEXT('CONST', 'ADDR_TYPE_IP') AND IPADR.N_OBJECT_ID=OSPEC.N_OBJECT_ID \
			WHERE G2.N_PARENT_GOOD_ID=:switch_root_id \
			");
			sth->setUInt(1, switch_root_id );
			ores = sth->executeQuery();
			if( ores->next() )
			{
				switchCount = ores->getInt(1);
				syslog(LOG_INFO, "Switches count for ping: %i\n", switchCount);
				if( !dmn )
					printf("Switches count for ping: %i\n", switchCount);
			} else {
				sleep(60);
				continue;
			}
			sth->closeResultSet( ores );
			ora->terminateStatement(sth);
			
		} catch ( SQLException &e ) {
			syslog( LOG_ERR, "Error during selecting switches count: %s\n", e.getMessage().c_str() );
			if( !dmn )
				fprintf( stderr, "Error during selecting switches count: %s\n", e.getMessage().c_str() );
			sleep(60);
			continue;
		}
		
		
		// get switches
		try {
			sth = ora->createStatement("\
			SELECT O.N_OBJECT_ID, IPADR.VC_VISUAL_CODE AS IP, \
			V1.VC_VISUAL_VALUE AS ALIVE, \
			V1.N_OBJ_VALUE_ID AS VALUE_ID \
			FROM SI_V_OBJECTS_SIMPLE O \
				INNER JOIN SR_V_GOODS_SIMPLE G \
				    ON G.N_GOOD_TYPE_ID=1 AND G.N_GOOD_ID=O.N_GOOD_ID \
				INNER JOIN SR_V_GOODS_SIMPLE G2 \
				    ON G2.N_GOOD_ID=G.N_PARENT_GOOD_ID \
				INNER JOIN SI_V_OBJECTS_SPEC_SIMPLE OSPEC \
				    ON OSPEC.N_MAIN_OBJECT_ID=O.N_OBJECT_ID AND OSPEC.VC_NAME LIKE 'CPU %' \
				INNER JOIN SI_V_OBJ_ADDRESSES_SIMPLE_CUR IPADR \
				    ON IPADR.N_ADDR_TYPE_ID=SYS_CONTEXT('CONST', 'ADDR_TYPE_IP') AND IPADR.N_OBJECT_ID=OSPEC.N_OBJECT_ID \
				LEFT JOIN SI_V_OBJ_VALUES V1 \
				    ON V1.N_OBJECT_ID=O.N_OBJECT_ID AND V1.N_GOOD_VALUE_TYPE_ID=:alive_param_id \
			WHERE G2.N_PARENT_GOOD_ID=:switch_root_id \
			");
			sth->setUInt(2, switch_root_id );
			sth->setUInt(1, alive_param_id );
			ores = sth->executeQuery();
			
		} catch ( SQLException &e ) {
			syslog( LOG_ERR, "Error during selecting switches list: %s\n", e.getMessage().c_str() );
			if( !dmn )
				fprintf( stderr, "Error during selecting switches list: %s\n", e.getMessage().c_str() );
			sleep(60);
			continue;
		}
		
		// Get switches per worker
		vector<vector < Switch > > sw;
		sw.resize( workers );
		
		int wrkr = 0;
		while( ores->next() ) {
			Switch s;
			try {
			
				s.id = ores->getNumber(1);
				s.ip = ores->getString(2);
				s.value_id = ores->getNumber(4);
				if( ores->getString(3) == "Y" )
					s.alive = true;
				else
					s.alive = false;
			} catch (SQLException &e) {
				syslog( LOG_ERR, "Can't parse switch data: %s", e.getMessage().c_str() );
				if( !dmn )
					fprintf( stderr, "Can't parse switch data: %s", e.getMessage().c_str() );
				continue;
			}
			
			sw[wrkr].push_back(s);
			if( wrkr != (workers-1) ) {
				wrkr++;
			} else {
				wrkr = 0;
			}
		}
		
		if( !dmn )
			printf("Switch per worker: %i\n", (int)sw[0].size());
		syslog(LOG_INFO, "Switch per worker: %i\n", (int)sw[0].size());
		
		/****************************************************************
			    FORK WORKERS HERE
		****************************************************************/
		boost::thread_group threads;
		for( int child=0; child<workers; child++ )
		{
			threads.create_thread( boost::bind(do_work, sw[child] ) );
		}
		
		threads.join_all();
		threads.interrupt_all();
		
		// all threads finished here.
		syslog( LOG_INFO, "All threads finished. Waiting 15 sec to reselect.\n");
		if( !dmn )
			printf( "All threads finished. Waiting 15 sec to reselect.\n");
		
		try {
			sth->closeResultSet(ores);
			ora->terminateStatement(sth);
			env->terminateConnection(ora);
		} catch (SQLException &e) {
			syslog(LOG_ERR, "Can't close connection: %s\n", e.getMessage().c_str());
			if( !dmn )
				fprintf(stderr, "Can't close connection: %s\n", e.getMessage().c_str());
			sleep(60);
			continue;
		}
		
		sleep(15);
	}
	exit(1);
}


int do_work( vector<Switch> s ) {

	for( int i=0; i<s.size(); i++ ) {
	
		bool result = false;
		
		for( int v=0; v<3; v++ ) {
			if( ping(s[i].ip.c_str() ) ) {
				result = true;
				break;
			} else {
				sleep(2);
			}
		}
	
		// if switch was alive, try 2 more times:
		if( !result && s[i].alive ) {
			sleep(5);
			for(int v=0; v<3; v++) {
				if( ping(s[i].ip.c_str() ) ) {
					result = true;
					break;
				} else {
					sleep(2);
				}
			}
		}
	
		// and again...
		if( result == 0 && s[i].alive ) {
			sleep(5);
			for(int v=0; v<3; v++) {
				if( ping(s[i].ip.c_str() ) ) {
					result = true;
					break;
				} else {
					sleep(2);
				}
			}
		}
//	}
	
	// Updating status
	if( result != s[i].alive ) {
		boost::unique_lock<boost::mutex> ownlock(mtx_);
		if( !dmn )
			fprintf(stderr, "Updating switch %s state.\n", s[i].ip.c_str());
		syslog(LOG_INFO,  "Updating switch %s state.\n", s[i].ip.c_str());
		
		std::string clive;
		if( result )
			clive = "Y";
		else
			clive = "N";
		
//		if( s[i].value_id.isNull() || s[i].value_id == 0 )
		
		try {
			Statement* st = ora->createStatement();
			
//			if( s[i].value_id.isNull() || s[i].value_id == 0 )
			if( s[i].value_id.isNull())
			{
				unsigned int num;
				st->setSQL("BEGIN SI_OBJECTS_PKG.SI_OBJ_VALUES_PUT( num_N_OBJECT_ID => :id, num_N_GOOD_VALUE_TYPE_ID => :type_id, ch_C_FL_VALUE => :clive, num_N_OBJ_VALUE_ID => :num ); END;");
				st->registerOutParam( 4, OCCIUNSIGNED_INT, sizeof(num) );
			} else {
				st->setSQL("BEGIN SI_OBJECTS_PKG.SI_OBJ_VALUES_PUT( num_N_OBJECT_ID => :id, num_N_GOOD_VALUE_TYPE_ID => :type_id, ch_C_FL_VALUE => :clive, num_N_OBJ_VALUE_ID => :num ); END;");
				st->setNumber( 4, s[i].value_id );
			}
			
			st->setNumber(1, s[i].id );
			st->setUInt(2, alive_param_id);
			st->setString(3, clive);
			st->execute();
			ora->commit();
			ora->terminateStatement(st);
			
		} catch(SQLException &e) {
			syslog(LOG_ERR, "Can't update switch status: %s\n", e.getMessage().c_str() );
			if( !dmn )
				fprintf(stderr, "Can't update switch status: %s\n", e.getMessage().c_str() );
			continue;
		}
	}
	
	}
}

bool ping(std::string host ){
	bool ret = false;
	pingobj_t *pingobj = ping_construct();
	if( pingobj == NULL )
	    return ret;
	
	
	for( ;; ) {
		double timeout_sec = 2.0;
		if( ping_setopt( pingobj, PING_OPT_TIMEOUT, &timeout_sec) ) {
			break;
		}
		
		if( ping_host_add( pingobj, host.c_str() ) )
			break;
		
		if( ping_send( pingobj) <= 0 )
			break;
		
		ret = true;
		break;
	}
	
	ping_destroy( pingobj );
	return ret;
}


void read_mem( statm_t& result)
{
	unsigned long dummy;
	const char* statm_path = "/proc/self/statm";
	
	FILE *f = fopen(statm_path, "r");
	if(!f){
		perror(statm_path);
		abort();
	}
	
	if( 7 != fscanf(f, "%ld %ld %ld %ld %ld %ld %ld",
		&result.size, &result.resident, &result.share, &result.text, &result.lib, &result.data, &result.dt))
	{
		perror(statm_path);
		abort();
	}
	fclose(f);
}











