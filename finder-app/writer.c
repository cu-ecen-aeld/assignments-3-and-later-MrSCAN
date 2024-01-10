#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

void writeToLogFile(const char *filename, const char *string) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        // Log unexpected error if file cannot be opened
        syslog(LOG_ERR, "Error opening file %s for writing: %m", filename);
        exit(EXIT_FAILURE);
    }

    // Write the string to the file
    fprintf(file, "%s\n", string);

    // Close the file
    fclose(file);

    // Log the operation with LOG_DEBUG level
    syslog(LOG_DEBUG, "Writing \"%s\" to %s", string, filename);
}

int main(int argc, char *argv[]) {
    // Check if the correct number of arguments is provided
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <filename> <string>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Open syslog with LOG_USER facility
    openlog(argv[0], LOG_PID | LOG_NDELAY, LOG_USER);

    // Call the function to write to the log file
    writeToLogFile(argv[1], argv[2]);

    // Close syslog
    closelog();

    return EXIT_SUCCESS;
}
