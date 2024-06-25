#include <stdio.h>

void explanation() {
    printf("\n==================BANAN NETWORK==================\n");
    printf("Run & train a neural network from within Banan-OS\n=================================================");
    printf("\nUse `startnetwork train` to train a neural network on a test XOR dataset.\nUse `startnetwork run` to run this network on a test dataset.\n");
    printf("You can obviously only run the network after it has been trained.\n");
}

int main(int argc, char** argv) {
    explanation();
    return 0;
}
