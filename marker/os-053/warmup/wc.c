#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "common.h"
#include "wc.h"
#include <string.h>
#include <stdbool.h>

long SIZE; //10000000;//4206969727; //4.2069bil
unsigned long hash(char *str);


typedef struct item {
	char * letters;
	int repeats;
	struct item * nextItem;
}Item;

struct wc
{
	Item ** items;
};
struct wc * wc_init(char *word_array, long size)
{
	SIZE = 2 * size;
	char * arr = strdup(word_array);
	char * txt = strtok(arr," \t\v\r\n\f");


	struct wc *wc;
	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);

	wc->items = (Item **)malloc(SIZE * sizeof(Item*));
	assert(wc->items);
	
	

	for (; txt != NULL; txt = strtok(NULL, " \t\v\r\n\f"))
	{
		//the temp value that will be inserted
		Item * insertion = (Item *)malloc(sizeof(Item));
		insertion->letters = txt;
		insertion->nextItem = NULL;
		insertion->repeats = 1;

		unsigned long index = hash(txt);
		
		if (wc->items[index] == NULL){ //if hash value index is empty
			wc->items[index] = insertion;
		} 
		else {
			Item * ptr = wc->items[index];
			while (ptr != NULL){
				if (strcmp(insertion->letters, ptr->letters) == 0){
					ptr->repeats += 1;
					break;
				} else if (ptr->nextItem == NULL){
					ptr->nextItem = insertion;
					break;
				}
				ptr = ptr->nextItem;
			}
		}
	}
	return wc;
}

void wc_output(struct wc *wc)
{

	for (long i = 0; i < SIZE; i++){
		if (wc->items[i] != NULL){
			Item * ptr = wc->items[i];
			while (ptr != NULL){
				printf("%s:%d\n", ptr->letters, ptr->repeats);
				ptr = ptr->nextItem;
			}
		}
	}
}

void wc_destroy(struct wc *wc)
{
	for (long i = 0; i < SIZE; i++){
		if (wc->items[i] != NULL){
			if (wc->items[i]->nextItem != NULL){
				Item * ptrPrev = wc->items[i]->nextItem;
				Item * ptrNext = wc->items[i]->nextItem;
				while(ptrNext->nextItem != NULL){
					ptrNext = ptrNext->nextItem;
					free(ptrPrev);
					ptrPrev = ptrNext;
				}
				free(ptrNext);
			}
			free(wc->items[i]);
		}
	}
	free(wc->items);
	free(wc);
}

unsigned long hash(char *str)
{
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = hash * 33 ^ c;

	return hash%SIZE;
}