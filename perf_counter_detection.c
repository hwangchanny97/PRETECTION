#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sched.h>
#include <Python.h>

#define MAX_EVENTS 5

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

// File descriptors for multiple cores and events
int fds[MAX_EVENTS][1024]; // Assuming a maximum of 1024 cores
FILE *csv_file;
char output_filename[256] = "kaslrtp_monitor.csv"; // Default output file name
int num_cores;

void handle_sigint(int sig) {
    printf("\nTerminating the monitoring...\n");
    for (int i = 0; i < MAX_EVENTS; ++i) {
        for (int j = 0; j < num_cores; ++j) {
            if (fds[i][j] != -1) {
                ioctl(fds[i][j], PERF_EVENT_IOC_DISABLE, 0);
                close(fds[i][j]);
            }
        }
    }
    if (csv_file != NULL) {
        fclose(csv_file);
    }
    Py_Finalize(); // Finalize Python interpreter
    exit(0);
}

void run_monitoring_code() {
    struct perf_event_attr pe[MAX_EVENTS];
    memset(pe, 0, sizeof(struct perf_event_attr) * MAX_EVENTS);

    // Event definitions
    /*{
    "EventName": "ls_l1_d_tlb_miss.all",
    "EventCode": "0x45",
    "BriefDescription": "L1 DTLB Miss or Reload off all sizes.",
    "UMask": "0xff"
    }*/
    pe[0].type = PERF_TYPE_RAW;
    pe[0].size = sizeof(struct perf_event_attr);
    pe[0].config = (0xff << 8) | 0x45;
    pe[0].disabled = 1;
    pe[0].exclude_kernel = 1;
    pe[0].exclude_hv = 1;

    /*{
    "EventName": "ls_tablewalker.dside",
    "EventCode": "0x46",
    "BriefDescription": "Total Page Table Walks on D-side.",
    "UMask": "0x03"
    }*/
    pe[1].type = PERF_TYPE_RAW;
    pe[1].size = sizeof(struct perf_event_attr);
    pe[1].config = (0x03 << 8) | 0x46;
    pe[1].disabled = 1;
    pe[1].exclude_kernel = 1;
    pe[1].exclude_hv = 1;

    /*{
    "EventName": "ls_inef_sw_pref.data_pipe_sw_pf_dc_hit",
    "EventCode": "0x52",
    "BriefDescription": "The number of software prefetches that did not fetch data outside of the processor core. Software PREFETCH instruction saw a DC hit.",
    "UMask": "0x01"
    }*/
    pe[2].type = PERF_TYPE_RAW;
    pe[2].size = sizeof(struct perf_event_attr);
    pe[2].config = (0x01 << 8) | 0x52;
    pe[2].disabled = 1;
    pe[2].exclude_kernel = 1;
    pe[2].exclude_hv = 1;

    /*{
    "EventName": "ls_mab_alloc.stores",
    "EventCode": "0x41",
    "BriefDescription": "LS MAB allocates by type - stores.",
    "UMask": "0x02"
    }*/
    pe[3].type = PERF_TYPE_RAW;
    pe[3].size = sizeof(struct perf_event_attr);
    pe[3].config = (0x02 << 8) | 0x41;
    pe[3].disabled = 1;
    pe[3].exclude_kernel = 1;
    pe[3].exclude_hv = 1;

    /*{
    "EventName": "ls_dc_accesses",
    "EventCode": "0x40",
    "BriefDescription": "The number of accesses to the data cache for load and store references. This may include certain microcode scratchpad accesses, although these are generally rare. Each increment represents an eight-byte access, although the instruction may only be accessing a portion of that. This event is a speculative event."
    }*/
    pe[4].type = PERF_TYPE_RAW;
    pe[4].size = sizeof(struct perf_event_attr);
    pe[4].config = (0x01 << 8) | 0x40;
    pe[4].disabled = 1;
    pe[4].exclude_kernel = 1;
    pe[4].exclude_hv = 1;

    // Get the number of cores available
    num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    printf("Number of cores: %d\n", num_cores);

    // Open performance events for all cores
    for (int i = 0; i < MAX_EVENTS; ++i) {
        for (int j = 0; j < num_cores; ++j) {
            fds[i][j] = perf_event_open(&pe[i], -1, j, -1, 0);
            if (fds[i][j] == -1) {
                perror("perf_event_open");
                for (int k = 0; k <= i; ++k) {
                    for (int l = 0; l <= j; ++l) {
                        if (fds[k][l] != -1) {
                            close(fds[k][l]);
                        }
                    }
                }
                exit(EXIT_FAILURE);
            }
        }
    }

    // Open the CSV file to write the results
    csv_file = fopen(output_filename, "w");
    if (csv_file == NULL) {
        perror("fopen");
        for (int i = 0; i < MAX_EVENTS; ++i) {
            for (int j = 0; j < num_cores; ++j) {
                close(fds[i][j]);
            }
        }
        exit(EXIT_FAILURE);
    }

    // Write the CSV header
    fprintf(csv_file, "Timestamp(ms),L1 DTLB Misses,Tablewalker D-side,Software Prefetch DC Hit,LS MAB Alloc Stores,LS DC Accesses\n");
    fflush(csv_file);  // Ensure header is written to disk immediately

    // Set up the signal handler to gracefully exit
    signal(SIGINT, handle_sigint);

    // Record the start time
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Enable all the events initially
    for (int i = 0; i < MAX_EVENTS; ++i) {
        for (int j = 0; j < num_cores; ++j) {
            ioctl(fds[i][j], PERF_EVENT_IOC_RESET, 0);
            ioctl(fds[i][j], PERF_EVENT_IOC_ENABLE, 0);
        }
    }

    // Loop to continuously output the result every 1ms
    printf("Child process: Monitoring multiple events on all cores every 1ms... (Press Ctrl+C to stop)\n");
    while (1) {
        long long aggregated_counts[MAX_EVENTS] = {0};

        // Read and aggregate the counter values for each event across all cores
        for (int i = 0; i < MAX_EVENTS; ++i) {
            for (int j = 0; j < num_cores; ++j) {
                long long count;
                if (read(fds[i][j], &count, sizeof(long long)) == -1) {
                    perror("read");
                    for (int k = 0; k < MAX_EVENTS; ++k) {
                        for (int l = 0; l < num_cores; ++l) {
                            close(fds[k][l]);
                        }
                    }
                    fclose(csv_file);
                    exit(EXIT_FAILURE);
                }
                aggregated_counts[i] += count;
            }
        }

        // Get the current time and calculate elapsed time in ms since start
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        long long elapsed_time_ms = (current_time.tv_sec - start_time.tv_sec) * 1000LL
                                  + (current_time.tv_nsec - start_time.tv_nsec) / 1000000LL;

        // Write the timestamp and aggregated counts to the CSV file
        fprintf(csv_file, "%lld,%lld,%lld,%lld,%lld,%lld\n",
                elapsed_time_ms, aggregated_counts[0], aggregated_counts[1],
                aggregated_counts[2], aggregated_counts[3], aggregated_counts[4]);

        // Flush the file buffer to ensure data is written to disk immediately
        fflush(csv_file);

        // Reset the counters after each read
        for (int i = 0; i < MAX_EVENTS; ++i) {
            for (int j = 0; j < num_cores; ++j) {
                ioctl(fds[i][j], PERF_EVENT_IOC_RESET, 0);
            }
        }

        // Sleep for 1ms
        usleep(1000);
    }
}

void load_model() {
    // Initialize the Python interpreter
    Py_Initialize();
    printf("Parent process: Loading trained model using embedded Python...\n");

    // Load the trained model in Python
    PyRun_SimpleString(
        "import joblib\n"
        "global model\n"
        "model = joblib.load('random_forest_model.pkl')\n"
        
    );

    printf("Model loaded successfully.\n");
}

void predict_from_csv() {
    printf("Parent process: Starting to read CSV file and make predictions using embedded Python...\n");

    // Initialize Python components for model, scaler, and column list
    PyRun_SimpleString(
        "import pandas as pd\n"
        "import joblib\n"
        "scaler = joblib.load('rf_scaler.pkl')\n"
        // xgboost_model, XGB_scaler / LR_model, LR_scaler / random_forest_model, rf_scaler
        //"model = joblib.load('dt_model.pkl')\n"
        "feature_columns = [\n"
        "    'Tablewalker D-side',\n"
        "    'LS MAB Alloc Stores',\n"
        "    'Software Prefetch DC Hit',\n"
        "    'LS DC Accesses',\n"
        "    'L1 DTLB Misses'\n"
        "]\n"
    );

    while (1) {
        struct timespec iteration_start, iteration_end;
        clock_gettime(CLOCK_MONOTONIC, &iteration_start);  // Start timing the iteration

        // Run the Python code to read, process, and predict the last row in the CSV
        PyRun_SimpleString(
            "try:\n"
            "    # Load the CSV and select the last row with specified columns\n"
            "    file_path = '/home/chan/Desktop/experiment/work_load_data/real_data/temp.csv'\n"
            "    data = pd.read_csv(file_path)\n"
            "    data = data[feature_columns].tail(1)  # Ensure the correct columns and get the last row\n"
            "\n"
            "    # Scale the features and make a prediction\n"
            "    scaled_features = scaler.transform(data)\n"
            "    prediction = model.predict(scaled_features)\n"
            "    print(f'Prediction for the last row: {prediction[0]}')\n"
            "except Exception as e:\n"
            "    print(f'Error in prediction: {e}')\n"
            
        );

        // Record the end time of the iteration
        clock_gettime(CLOCK_MONOTONIC, &iteration_end);

        // Calculate elapsed time in milliseconds for the iteration
        double elapsed_time_ms = (iteration_end.tv_sec - iteration_start.tv_sec) * 1000.0 +
                                 (iteration_end.tv_nsec - iteration_start.tv_nsec) / 1000000.0;

        printf("time:%.3f\n", elapsed_time_ms);
        // Sleep for 10ms before reading the next line
        usleep(10000);
    }
}


int main(int argc, char *argv[]) {
    // Check if output file name is provided
    if (argc > 1) {
        snprintf(output_filename, sizeof(output_filename), "%s.csv", argv[1]);
    } else {
        printf("No output filename provided. Using default: %s\n", output_filename);
    }

    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process: Run the monitoring code
        run_monitoring_code();
    } else {
        // Parent process: Load the model and start predictions
        load_model();
        predict_from_csv();

        // Wait for child process to finish
        wait(NULL);
    }

    return 0;
}
