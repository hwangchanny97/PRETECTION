#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define MAX_DATA_ENTRIES 100000

typedef struct {
    double timestamp;
    long count;
    char event_name[50];
} PerfStatData;

double get_elapsed_time(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <interval_count> <output_csv_file>\n", argv[0]);
        return 1;
    }

    // Start timing for the entire process
    struct timespec start_total, end_total;
    clock_gettime(CLOCK_MONOTONIC, &start_total);

    int interval_count = atoi(argv[1]);
    char *output_file = argv[2];

    char buffer[BUFFER_SIZE];
    FILE *pipe;
    PerfStatData data_array[MAX_DATA_ENTRIES];
    int data_index = 0;
    int header_written = 0;  // Track if header has been written

    // Build the perf command with interval_count and capture output in CSV format
    char command[512];
    sprintf(command, "sudo perf stat -x, -e ls_pref_instr_disp,ls_pref_instr_disp.prefetch,ls_pref_instr_disp.prefetch_nta,ls_l1_d_tlb_miss.all,ls_inef_sw_pref.mab_mch_cnt -a -I 1 --interval-count %d 2>&1", interval_count);
    
    // Measure time for data collection
    struct timespec start_data_collection, end_data_collection;
    clock_gettime(CLOCK_MONOTONIC, &start_data_collection);

    // Execute the perf command
    pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Failed to run perf stat\n");
        return 1;
    }

    // Open CSV file for writing
    FILE *csv_file = fopen(output_file, "a");
    if (!csv_file) {
        fprintf(stderr, "Failed to open CSV file for writing\n");
        return 1;
    }

    // Write header once at the beginning
    if (!header_written) {
        fprintf(csv_file, "Timestamp,ls_pref_instr_disp,ls_pref_instr_disp.prefetch,ls_pref_instr_disp.prefetch_nta,ls_l1_d_tlb_miss.all,ls_inef_sw_pref.mab_mch_cnt\n");
        header_written = 1;
    }

    // Parse perf output and populate the data array
    while (fgets(buffer, sizeof(buffer), pipe) != NULL && data_index < interval_count+2) {
        char *token = strtok(buffer, ",");
        int count = 0;
        PerfStatData entry;

        while (token != NULL) {
            count++;
            if (count == 1) {
                entry.timestamp = atof(token);  // Convert string to double
            } else if (count == 2) {
                entry.count = atol(token);  // Convert string to long
            } else if (count == 3) {
                strncpy(entry.event_name, token, sizeof(entry.event_name) - 1);
                entry.event_name[sizeof(entry.event_name) - 1] = '\0';  // Null-terminate
            }
            token = strtok(NULL, ",");
        }
        data_array[data_index++] = entry;  // Store parsed entry

        // When a full row of data is collected, write to CSV and reset index
        if (data_index == 5) {  // Assuming 5 events per sample
            fprintf(csv_file, "%.3f,%ld,%ld,%ld,%ld,%ld\n",
                    data_array[0].timestamp, data_array[0].count, data_array[1].count,
                    data_array[2].count, data_array[3].count, data_array[4].count);

            data_index = 0;  // Reset index for the next row of data
        }
    }

    pclose(pipe);
    fclose(csv_file);

    // Measure end time for data collection
    clock_gettime(CLOCK_MONOTONIC, &end_data_collection);

    //Additional code for timing can be uncommented if needed
    clock_gettime(CLOCK_MONOTONIC, &end_total);
    double data_collection_time = get_elapsed_time(start_data_collection, end_data_collection);
    double total_time = get_elapsed_time(start_total, end_total);


    return 0;
}
