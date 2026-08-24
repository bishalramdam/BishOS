#include <stdio.h>
#include <unistd.h>

int main() {
    // Disable stdout buffering so characters are printed immediately to the serial console
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\n");
    printf("==========================================\n");
    printf("        Welcome to BishOS!                \n");
    printf("  Running bare-metal Linux Userspace      \n");
    printf("  PID: %d                                \n", getpid());
    printf("==========================================\n");
    printf("\n");
    printf("Kernel booted successfully into /init!\n");
    printf("System is now running in an idle loop...\n\n");

    // PID 1 must NEVER return or exit.
    // If PID 1 exits, the Linux kernel panics and halts the CPU.
    while (1) {
        sleep(5);
        printf("[BishOS heartbeat] Still running as PID %d...\n", getpid());
    }

    return 0;
}
