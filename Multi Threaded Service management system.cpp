#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <queue>
#include <thread>
#include <mutex>
#include <pthread.h>
#include <chrono>
#include <algorithm>
#include <bits/stdc++.h>
using namespace std;
using namespace chrono;

struct Request
{
    int req_id;
    int transaction_type;
    int num_resources;
    high_resolution_clock::time_point arrival_time;
    high_resolution_clock::time_point start_time;
    high_resolution_clock::time_point completion_time;
    milliseconds waiting_time;
    milliseconds turnaround_time;
};

struct WorkerThread
{
    int priority;
    int num_resources;
    int available_resources;
    pthread_mutex_t workerthread_mutex;
    bool is_completed_all_requests;
};

struct Service
{
    int type;
    int num_threads;
    queue<Request *> process_queue;
    pthread_mutex_t queue_mutex;
    vector<WorkerThread> threads;
    bool is_completed_all_requests;
};

vector<Service> services;
int rejected_request_count = 0;
int forced_waited_request_count = 0;
int blocked_request_count = 0;
pthread_mutex_t rejected_count;
pthread_mutex_t waited_count;
pthread_mutex_t blocked_count;
vector<int> exec;

// Function will check if service queue is empty or not.
bool is_service_queue_empty(int service_id)
{
    pthread_mutex_lock(&services[service_id].queue_mutex);
    bool ret = services[service_id].process_queue.empty();
    pthread_mutex_unlock(&services[service_id].queue_mutex);
    return ret;
}

// Get front request from service queue
Request *get_service_queue_front(int service_id)
{
    pthread_mutex_lock(&services[service_id].queue_mutex);
    Request *ret = services[service_id].process_queue.front();
    services[service_id].process_queue.pop();
    pthread_mutex_unlock(&services[service_id].queue_mutex);
    return ret;
}

// Push request to service queue using service type
void service_queue_push(int service_id, Request *req)
{
    pthread_mutex_lock(&services[service_id].queue_mutex);
    services[service_id].process_queue.push(req);
    pthread_mutex_unlock(&services[service_id].queue_mutex);
}

// Check execution queue is empty or not.
bool is_execution_queue_empty(int service_id, int thread_id, queue<Request *> &execution_queue)
{
    pthread_mutex_lock(&services[service_id].threads[thread_id].workerthread_mutex);
    bool ret = execution_queue.empty();
    pthread_mutex_unlock(&services[service_id].threads[thread_id].workerthread_mutex);
    return ret;
}

// Get front request from execution queue
Request *get_execution_queue_front(int service_id, int thread_id, queue<Request *> &execution_queue)
{
    pthread_mutex_lock(&services[service_id].threads[thread_id].workerthread_mutex);
    Request *req = execution_queue.front();
    execution_queue.pop();
    pthread_mutex_unlock(&services[service_id].threads[thread_id].workerthread_mutex);
    return req;
}

// Main function to execute request, assuming each request take around 5 seconds of time
void execute_request(int service_id, int thread_id, Request *req)
{
    req->start_time = high_resolution_clock::now(); // Execution starting time of request
    cout << "Service: " << service_id << " thread: " << thread_id << " request: " << req->req_id << " execution is started." << endl;

    exec.push_back(req->req_id);
    sleep(5);

    cout << "Service: " << service_id << " thread: " << thread_id << " request: " << req->req_id << " execution is completed. " << endl;

    req->completion_time = high_resolution_clock::now(); // Execution completion time of request
    pthread_mutex_lock(&services[service_id].threads[thread_id].workerthread_mutex);
    services[service_id].threads[thread_id].available_resources += req->num_resources; // Updating available resources
    pthread_mutex_unlock(&services[service_id].threads[thread_id].workerthread_mutex);
    req->turnaround_time = duration_cast<milliseconds>(req->completion_time - req->arrival_time); // TAT = CT - AT
    req->waiting_time = duration_cast<milliseconds>(req->start_time - req->arrival_time);         // WT = TAT - BT
}

// This method represent one worker thread of service, based on available resources it will create threads to execute requests.
void worker_thread_function(int service_id, int thread_id, queue<Request *> &execution_queue)
{
    vector<thread> worker_thread_instances;
    while (true)
    {
        if (!is_execution_queue_empty(service_id, thread_id, execution_queue))
        {
            Request *req = get_execution_queue_front(service_id, thread_id, execution_queue);
            worker_thread_instances.emplace_back(execute_request, service_id, thread_id, req);
        }
        else if (services[service_id].threads[thread_id].is_completed_all_requests)
        {
            break;
        }
        usleep(10);
    }

    for (thread &instance : worker_thread_instances)
    {
        instance.join();
    }
}

bool comparator(WorkerThread t1, WorkerThread t2)
{
    return t1.priority < t2.priority;
}

// This method is main function for service, it will schedule particular type of request to higher priority queue.
void service_function(int service_id)
{
    thread worker_threads[services[service_id].num_threads];            // Worker threads
    queue<Request *> execution_queue[services[service_id].num_threads]; // To assign request to proper worker thread.

    sort(services[service_id].threads.begin(), services[service_id].threads.end(), comparator); // sort worker based on priority

    for (int thread_id = 0; thread_id < services[service_id].num_threads; thread_id++)
    {
        pthread_mutex_init(&services[service_id].threads[thread_id].workerthread_mutex, NULL);
        cout << " Thread Info: "
             << "Service: " << services[service_id].type << " thread: " << thread_id << " priority: " << services[service_id].threads[thread_id].priority << " resources: " << services[service_id].threads[thread_id].num_resources << endl;

        worker_threads[thread_id] = thread(worker_thread_function, service_id, thread_id, ref(execution_queue[thread_id]));
    }

    int flag1 = 0;
    int flag2 = 0;
    int flag3 = 0;
    while (true)
    {
        if (!is_service_queue_empty(service_id))
        {
            Request *req = get_service_queue_front(service_id); // Get particular type of request from queue

            int threads_with_less_resources = 0;
            bool all_threads_occupied = true, req_assigned = false;

            for (int thread_id = 0; thread_id < services[service_id].num_threads; thread_id++)
            {
                pthread_mutex_lock(&services[service_id].threads[thread_id].workerthread_mutex);
                if (req->num_resources <= services[service_id].threads[thread_id].available_resources)
                { // request require less resources than available resources of higher priority queue
                    services[service_id].threads[thread_id].available_resources -= req->num_resources;
                    cout << " Service: " << service_id << " requested resources: " << req->num_resources << " assigned to thread: " << thread_id << " available " << services[service_id].threads[thread_id].available_resources << endl;
                    execution_queue[thread_id].push(req);
                    pthread_mutex_unlock(&services[service_id].threads[thread_id].workerthread_mutex);
                    all_threads_occupied = false;
                    req_assigned = true;
                    break;
                }
                else if (req->num_resources > services[service_id].threads[thread_id].num_resources)
                { // Check number of threads with less resources
                    threads_with_less_resources++;
                }
                if (services[service_id].threads[thread_id].num_resources == services[service_id].threads[thread_id].available_resources)
                { // Check for free threads
                    all_threads_occupied = false;
                }
                pthread_mutex_unlock(&services[service_id].threads[thread_id].workerthread_mutex);
            }

            if (threads_with_less_resources == services[service_id].num_threads)
            { // Request require more resources than all thread resources, so rejecting it.

                if (flag1 == 0)
                    cout << "Services: " << service_id << " request id: " << req->req_id << " with resources: " << req->num_resources << " is rejected." << endl;
                flag1 = 1;
                pthread_mutex_lock(&rejected_count);
                rejected_request_count++;
                pthread_mutex_unlock(&rejected_count);
            }
            else if (!req_assigned)
            { // Not assigned request will go to waiting and again go to queue
                service_queue_push(service_id, req);
                if (all_threads_occupied)
                { // Request is blocked due to all threads are occupied
                    if (flag2 == 0)
                        cout << "Services: " << service_id << " request id: " << req->req_id << " with resources: " << req->num_resources << " is blocked." << endl;
                    flag2 = 1;
                    pthread_mutex_lock(&blocked_count);
                    blocked_request_count++;
                    pthread_mutex_unlock(&blocked_count);
                }
                else
                { // Request is forced to wait due to less available resources
                    if (flag3 == 0)
                        cout << "Services: " << service_id << " request id: " << req->req_id << " with resources: " << req->num_resources << " is added in waiting queue." << endl;
                    flag3 = 1;
                    pthread_mutex_lock(&waited_count);
                    forced_waited_request_count++;
                    pthread_mutex_unlock(&waited_count);
                }
            }
        }
        else if (services[service_id].is_completed_all_requests)
        {
            break;
        }
        usleep(100);
    }

    for (int thread_id = 0; thread_id < services[service_id].num_threads; thread_id++)
    {
        worker_threads[thread_id].join();
        pthread_mutex_destroy(&services[service_id].threads[thread_id].workerthread_mutex);
    }
}

int main()
{
    srand(time(NULL));
    int num_services, num_threads;

    cout << "Enter the number of services: ";
    cin >> num_services;

    cout << "Enter the number of threads per process: ";
    cin >> num_threads;

    services.resize(num_services);

    thread service_threads[num_services];

    for (int service_id = 0; service_id < num_services; service_id++)
    {
        services[service_id].type = service_id;
        services[service_id].num_threads = num_threads;
        services[service_id].is_completed_all_requests = false;
        pthread_mutex_init(&services[service_id].queue_mutex, NULL);
    }

    // Initial setup for services and worker threads
    for (int service_id = 0; service_id < num_services; service_id++)
    {
        for (int thread_id = 0; thread_id < num_threads; thread_id++)
        {
            int priority, resources;

            cout << "Enter priority and resources for Service " << service_id << " and thread " << thread_id << endl;
            cin >> priority >> resources;
            WorkerThread thread;
            thread.priority = priority;
            thread.num_resources = resources;
            thread.available_resources = resources;
            services[service_id].threads.push_back(thread);
            services[service_id].threads[thread_id].is_completed_all_requests = false;
        }
    }

    // Now getting requests
    int number_requests;

    cout << "Enter number of requests: ";
    cin >> number_requests;

    for (int service_id = 0; service_id < num_services; service_id++)
    {
        service_threads[service_id] = thread(service_function, service_id);
    }

    int req_id = 0;
    vector<Request *> requests;

    // Get all requests from user
    cout << "Enter request type and resources-" << endl;
    for (int i = 0; i < number_requests; i++)
    {
        int transaction_type, num_resources;
        cin >> transaction_type >> num_resources;

        if (transaction_type < 0 || transaction_type >= num_services)
        {

            cout << "Invalid transaction type " << transaction_type << endl;
            continue;
        }

        cout << "Request Id: " << req_id << " Transaction Type: " << transaction_type << " Number of Resources required: " << num_resources << "- Request Arrived." << endl;

        Request *req = new Request;
        req->req_id = req_id;
        req->transaction_type = transaction_type;
        req->num_resources = num_resources;
        req->arrival_time = high_resolution_clock::now();
        requests.push_back(req);
        service_queue_push(transaction_type, req);
        req_id++;
    }

    sleep(2);
    for (int service_id = 0; service_id < num_services; service_id++)
    {
        services[service_id].is_completed_all_requests = true;
        for (int thread_id = 0; thread_id < num_threads; thread_id++)
        {
            services[service_id].threads[thread_id].is_completed_all_requests = true;
        }
    }

    for (int service_id = 0; service_id < num_services; service_id++)
    {
        service_threads[service_id].join();
        pthread_mutex_destroy(&services[service_id].queue_mutex);
    }
    cout << "Order of requests executed : ";
    for (int i : exec)
    {
        cout << i << " ";
    }
    cout << endl;

    cout << "Number of requests are rejected: " << rejected_request_count << endl;

    for (Request *req : requests)
    {
        cout << "Request Id: " << req->req_id << " Waiting time: " << req->waiting_time.count() << "ms"
             << " Turnaround Time: " << req->turnaround_time.count() << "ms" << endl;
    }

    // Calculate average waiting and turn around time
    milliseconds average_waiting_time(0);
    milliseconds average_turnaround_time(0);
    int count = 0;
    for (Request *req : requests)
    {
        if (req->turnaround_time.count() != 0)
        {
            average_waiting_time += req->waiting_time;
            average_turnaround_time += req->turnaround_time;
            count += 1;
        }
    }
    average_waiting_time /= count;
    average_turnaround_time /= count;

    cout << "Average waiting Time: " << average_waiting_time.count() << "ms" << endl;
    cout << "Average Turnaround Time: " << average_turnaround_time.count() << "ms" << endl;

    return 0;
}
