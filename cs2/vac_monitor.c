


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <cs2_pid> <steam_pid>\n", argv[0]);
        return 1;
    }
    
    pid_t cs2_pid = atoi(argv[1]);
    pid_t steam_pid = atoi(argv[2]);
    
    printf("[Monitor] Watching if Steam (PID %d) reads CS2 (PID %d) memory\n", steam_pid, cs2_pid);
    printf("[Monitor] Press Ctrl+C to stop\n\n");
    
    char path[256];
    DIR* dir;
    struct dirent* ent;
    
    while (1) {
        
        snprintf(path, sizeof(path), "/proc/%d/fd", steam_pid);
        dir = opendir(path);
        
        if (dir) {
            while ((ent = readdir(dir)) != NULL) {
                char link[512];
                char target[1024];
                
                snprintf(link, sizeof(link), "/proc/%d/fd/%s", steam_pid, ent->d_name);
                ssize_t len = readlink(link, target, sizeof(target) - 1);
                
                if (len > 0) {
                    target[len] = '\0';
                    
                    
                    char cs2_proc[64];
                    snprintf(cs2_proc, sizeof(cs2_proc), "/proc/%d", cs2_pid);
                    
                    if (strstr(target, cs2_proc)) {
                        char timestamp[64];
                        time_t now = time(NULL);
                        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&now));
                        printf("[%s] Steam opened: %s (fd %s)\n", timestamp, target, ent->d_name);
                    }
                }
            }
            closedir(dir);
        }
        
        
        
        
        usleep(100000); 
    }
    
    return 0;
}
