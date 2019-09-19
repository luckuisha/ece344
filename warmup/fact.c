#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

int factorial(int num);

int main(int argc, char **argv)
{
	//check if theres input
	if (argc != 2) {
		printf("Huh?\n");
		return 0;
	}
	
	for (int i = 0; argv[1][i] != '\0'; i++){
		if (argv[1][0] == '0'){
			printf("Huh?\n");
			return 0;
		}
		if (!isdigit(argv[1][i])){
			printf("Huh?\n");
			return 0;
		}
	}

	int num = atoi(argv[1]);

	if (num > 12){
		printf("Overflow\n");
		return 0;
	}

	printf("%d\n", factorial(num));

	return 0;
}

int factorial (int num){
	if (num == 1){
		return num;
	}

	return num * factorial (num - 1);

}