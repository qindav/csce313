#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

#include <sys/time.h>
#include <cassert>
#include <assert.h>
#include <chrono>

#include <cmath>
#include <numeric>
#include <algorithm>

#include <list>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "reqchannel.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
using namespace std;

// Special function to update the current table
Histogram hist;
static void DisplayHis(int sig, siginfo_t *siginfo, void *context){
    system("clear");
    hist.print();
}

/*--------------------------------------------------------------------------*/
/* Structure for Thread Arguements */
/*--------------------------------------------------------------------------*/

struct reqThread{
    int n;
    string clientName;
    BoundedBuffer *buffer;
};
struct workThread{
    int w;   // total channels
    int workload;
    BoundedBuffer *requestBuffer;
    RequestChannel *chan ;
    BoundedBuffer *b1;
    BoundedBuffer *b2;
    BoundedBuffer *b3;
};
struct statisticsThread{
    int n;
    string request;
    BoundedBuffer *responseBuffer;
    Histogram *hist;
};


/*--------------------------------------------------------------------------*/
/* Function Calls*/
/*--------------------------------------------------------------------------*/

void* request_thread_function(void* arg) {
	reqThread *input = (reqThread*)arg;
	
    for(int i = 0; i < (*input).n; ++i) {
     (*input).buffer->push("data " + (*input).clientName);
        //cout << (*input).clientName + "-push request:";
        //cout << i<<endl;
    }
    pthread_exit(NULL);
}
void* worker_thread_function(void* arg) {
    workThread *input = (workThread*)arg;
    int w = (*input).w;
    RequestChannel *chan = (*input).chan ;
    cout << w;
    vector<RequestChannel*> workerChannels;
    RequestChannel* workerChannel;

    vector<string> requesters;
    requesters.resize(w);
    
    int iTotalProcessed =0;
    fd_set rs;
	FD_ZERO(&rs);
	int maxfd = 0;
	//Create request channels
	for(int i = 0; i < w; ++i)
	{
        chan->cwrite("newchannel");
        string s = chan->cread ();
           
        workerChannel= new RequestChannel(s, RequestChannel::CLIENT_SIDE);
        workerChannels.push_back(workerChannel);
    
       // cout<<workerChannel->read_fd()<<endl;
       // cout<<workerChannel->write_fd()<<endl;
        
        if (workerChannel->read_fd() > maxfd) maxfd = workerChannel->read_fd();
        if (workerChannel->write_fd() > maxfd) maxfd = workerChannel->write_fd();
    }
    cout << "channels created "<< maxfd <<endl;
    
    struct timeval tv;
    tv.tv_sec=2;
    tv.tv_usec=0;

    int retVal;
    int iErrorcount =0;
    bool bQuit = false;
    //"Prime the pump"
    for(int i=0; i<w; i++) {
        if((*input).requestBuffer->size() == 0) 
            break;
        string request = (*input).requestBuffer->pop();
        //cout<<" write request:" + request <<endl;
        workerChannels[i]->cwrite(request);
        requesters[i] = request;
    }
    
    while(true) {
        //Begin filling the request buffers
        FD_ZERO(&rs);
	    for(int i=0; i<w; i++)
	    {
	        FD_SET(workerChannels[i]->read_fd(), &rs);  
	    }
        retVal=select(maxfd+1, &rs, NULL, NULL, &tv );
        
        if(retVal==-1){ //too many errors, break.
            cout<<"Error"<<endl;
            iErrorcount++;
            if(iErrorcount>10) 
                break;
        }
        else if(retVal){
            //Go through each channel and check if you can put something in there
            for(int i=0; i<w; i++) {
                if(FD_ISSET(workerChannels[i]->read_fd(), &rs)) {
                    //cout << "Read fromc channel " << i <<endl;
                    string response = workerChannels[i]->cread();
                    string request =  requesters[i];
                    BoundedBuffer *rb;
                    if(request == "data John Smith")
                        rb =  (*input).b1;
                    else if(request == "data Jane Smith")
                        rb =  (*input).b2;
                    else if(request == "data Joe Smith")
                        rb =  (*input).b3;
                    
                    rb->push(response);
                    iTotalProcessed++;
                    //cout<<"push response for " + request + ":" << response<<endl;
                    
                    //need write second request to the channel
                    if((*input).requestBuffer->size()>0) {
                        request = (*input).requestBuffer->pop();
                        
                        //cout<<" write request:" + request <<endl;
                        workerChannels[i]->cwrite(request);
                        requesters[i] = request;
                     
                    }
                }
            }
        }
        else{
            //cout <<"time out"<<endl;
             iErrorcount++;
            //if(iErrorcount>1000) break;
        }
        // Begin Quitting
        if(iTotalProcessed >= (*input).workload){
            cout<<"Write Quit"<<endl;
            for(int i=0; i<w; i++) {
                workerChannels[i]->cwrite("quit");
                delete workerChannels[i];
            }
            break;
        }
        
       
    }
}
void* stat_thread_function(void* arg) {
    statisticsThread *input = (statisticsThread*)arg;
    Histogram *hist = (*input).hist;
    
    for(int i = 0; i < (*input).n; ++i) {
        string response = (*input).responseBuffer->pop();
        hist->update ((*input).request, response);
        //cout<<(*input).request + ":update:" + response <<endl;
    }
    hist->print();
}

/*--------------------------------------------------------------------------*/
/* MAIN FUNCTION */
/*--------------------------------------------------------------------------*/

int main(int argc, char * argv[]) {
    int n = 2; //default number of requests per "patient"
    int w = 1; //default number of worker channel
    int b = 3 * n; // default capacity of the request buffer, you should change this default
    int opt = 0;
    
    //Begin timer
    auto start = std::chrono::steady_clock::now();
    
    //Read arguements and set nuumbers
    while ((opt = getopt(argc, argv, "n:w:b:")) != -1) {
        switch (opt) {
            case 'n':
                n = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg); //This won't do a whole lot until you fill in the worker thread function
                break;
            case 'b':
                b = atoi (optarg);
                break;
		}
    }

    //SIGNALS AND TIMERS
    struct sigaction act;
    act.sa_sigaction = DisplayHis;
    act.sa_flags = SA_SIGINFO;
    if(sigaction(SIGALRM, &act, NULL)<0){
         EXITONERROR ("");
    }
    timer_t timerid;
     
    struct sigevent sevt;
    sevt.sigev_notify = SIGEV_SIGNAL;
    sevt.sigev_signo=SIGALRM;
    sevt.sigev_value.sival_ptr = &timerid;
    
    timer_create(CLOCK_REALTIME, &sevt, &timerid);
    
    struct itimerspec value; 
    value.it_interval.tv_sec = 2; 
    value.it_interval.tv_nsec = 0; 
    value.it_value.tv_sec = 2; 
    value.it_value.tv_nsec = 0; 
   
   
    //PROCESS BEGINS HERE
    int pid = fork();
	if (pid == 0){
		execl("dataserver", (char*) NULL);
	}
	else {
        cout << "n == " << n << endl;
        cout << "w == " << w << endl;
        cout << "b == " << b << endl;
        cout << "CLIENT STARTED:" << endl;
        cout << "Establishing control channel... " << flush;
        RequestChannel *chan = new RequestChannel("control", RequestChannel::CLIENT_SIDE);
        cout << "done." << endl<< flush;
        
        
        BoundedBuffer request_buffer(b);
        timer_settime(timerid,0,&value, NULL);
		
	    //create 3 request threads
        reqThread John, Joe, Jane;
        void *res;
        John.n = n; John.clientName = "John Smith";  John.buffer = &request_buffer;
        pthread_t thread_id_John;
	    if (pthread_create(& thread_id_John, NULL, request_thread_function, (void*)&John) < 0 ) {
		   EXITONERROR ("");
     	}

      	Jane.n = n; Jane.clientName = "Jane Smith"; Jane.buffer = &request_buffer;
     	pthread_t thread_id_Jane;
	    if (pthread_create(& thread_id_Jane, NULL, request_thread_function,(void*)&Jane) < 0 ) {
		   EXITONERROR ("");
     	}

	    Joe.n = n; Joe.clientName = "Joe Smith"; Joe.buffer = &request_buffer;
     	pthread_t thread_id_Joe;
	    if (pthread_create(& thread_id_Joe , NULL, request_thread_function, (void*)&Joe) < 0 ) {
		   EXITONERROR ("");
     	}
     
	   
	    //Create bound buffers
	    BoundedBuffer* response_buffer1 = new BoundedBuffer(n);
	    BoundedBuffer* response_buffer2 = new BoundedBuffer(n);
	    BoundedBuffer* response_buffer3 = new BoundedBuffer(n);
   
        //Create work thread
        workThread inputs;
        pthread_t thread_id_Work;
      
        inputs.w=w;
        inputs.workload = 3*n;
        inputs.chan=chan;
        inputs.requestBuffer = &request_buffer;
        inputs.b1 = response_buffer1;
        inputs.b2 = response_buffer2;
        inputs.b3 = response_buffer3;

        if (pthread_create(&thread_id_Work, NULL, worker_thread_function, (void*)&inputs) < 0 ) {
		       EXITONERROR ("");
     	}
          
        
        //Creates stats
        statisticsThread input1,input2,input3;
        input1.hist = &hist;
        input1.responseBuffer = response_buffer1;
        input1.n=n;
        input1.request = "data John Smith"; 
        
        input2.hist = &hist;
        input2.responseBuffer = response_buffer2;
        input2.n=n;
        input2.request = "data Jane Smith"; 
        
        input3.hist = &hist;
        input3.responseBuffer = response_buffer3;
        input3.n=n;
        input3.request = "data Joe Smith"; 
       
       //Create static threads
       
        pthread_t s1,s2,s3;
        pthread_create(&s1, NULL, stat_thread_function, (void*)&input1);
        pthread_create(&s2, NULL, stat_thread_function, (void*)&input2);
        pthread_create(&s3, NULL, stat_thread_function, (void*)&input3);
        
        
        pthread_join(thread_id_John, 0);
        pthread_join(thread_id_Joe, 0);
        pthread_join(thread_id_Jane, 0);
        
        //Join threads
        pthread_join(s1, 0);
        pthread_join(s2, 0);
        pthread_join(s3, 0);
        
        /*
        //Push Quits
        cout << "Done populating request buffer" << endl;
        cout << "Pushing quit requests... ";
        //one work thread one quit need
        //for(int i = 0; i < w; ++i) {
        request_buffer.push("quit");
        //}
        cout << "done." << endl;
*/
        
        
        //Join worker threads, delete control
       
        pthread_join(thread_id_Work, 0);
        
        //delete control
        chan->cwrite ("quit");
        delete chan;
        cout << "All Done!!!" << endl;
        auto end = std::chrono::steady_clock::now();
        double nsec = std::chrono::nanoseconds(end - start).count();
        cout << "Time taken: " << nsec/1000000000.0 << "s" << std::endl;
        hist.print ();
    }
}


