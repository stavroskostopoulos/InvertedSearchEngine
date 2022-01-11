/*
 * core.cpp version 1.0
 * Copyright (c) 2013 KAUST - InfoCloud Group (All Rights Reserved)
 * Author: Amin Allam
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include"../../include/common_types.h"
#include"../../include/core.h"
#include"../../include/methods.h"
#include"../../include/bkt.h"


Core core=NULL;
///////////////////////////////////////////////////////////////////////////////////////////////

// This is a Test
int four=1;
static Pointer trial(Core core){
	pthread_mutex_lock(&core->job_scheduler->mutex);
	printf("I am the newly created thread %ld \n",(long)pthread_self());
	printf("NUM: %d \n",four);
	four++;
	pthread_mutex_unlock(&core->job_scheduler->mutex);
	return 0;
}

ErrorCode InitializeIndex(){
	core=core_create();
	
	// Initialize any thread at any given time
	// Not necessary here
	// This is a test
	for(int i=0;i<core->job_scheduler->num_threads;i++){
		thread_init(core->job_scheduler->threads[i],trial);
	}
	return EC_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////

ErrorCode DestroyIndex(){
	core_destroy(core);
	return EC_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////

ErrorCode StartQuery(QueryID query_id, const char* query_str, MatchType match_type, unsigned int match_dist){
	// Cast to String to not disqualify const expression
    String str=strdup((String)query_str);
	// Each word of the String
	String word=strtok(str," ");
	// Get each word
	while(word!=NULL){
		// Clean each word
		remove_special_characters_decapitalize(word);
		// Insert at the MatchType Dictionary
		if(match_type==MT_EXACT_MATCH){
			dictionary_insert(core->exact_queries, word,query_id); 
		} 
		else if(match_type==MT_EDIT_DIST){
			dictionary_insert(core->edit_queries, word,query_id);
		} 
		else{
			dictionary_insert(core->hamming_queries, word,query_id);
		}

		word=strtok(NULL," ");
	}
	free(str);
	
	// Insert at the MatchType[match_dist] QueryMap
	// The cleaning of each word of the whole query happens at QueryMap
	if(match_type==MT_EXACT_MATCH){
		query_map_insert(core->query_exact_map,query_id,(String)query_str,match_type,match_dist);
	}
	else if(match_type==MT_EDIT_DIST){

		query_map_insert(core->query_edit_map[match_dist],query_id,(String)query_str,match_type,match_dist);
	}
	else{
		query_map_insert(core->query_hamming_map[match_dist],query_id,(String)query_str,match_type,match_dist);
	}

	return EC_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////

ErrorCode EndQuery(QueryID query_id){
	Query query=NULL;
	// Start from the only EXACT MATCH QueryMap
	query=query_map_find(core->query_exact_map,query_id);
	// If Query was at EXACT MATCH QueryMap
	if(query!=NULL){
		// Remove the Entries
		dictionary_remove(core->exact_queries,core->query_exact_map,query_id);
		// Remove the Query
		query_map_remove(core->query_exact_map,query_id);
		return EC_SUCCESS;
	}
	else{
		// Find Query from the MatchType[match_dist] active Query set
		for(int i=0;i<4;i++){
			// Check whether Query is active at Edit Distance QueryMap[i]
			query=query_map_find(core->query_edit_map[i],query_id);
			if(query!=NULL){
				// Remove the Entries
				dictionary_remove(core->edit_queries,core->query_edit_map[i],query_id);
				// Remove the Query
				query_map_remove(core->query_edit_map[i],query_id);
				return EC_SUCCESS;
			}
			// Check whether Query is active at Hamming Distance QueryMap[i]
			query=query_map_find(core->query_hamming_map[i],query_id);
			if(query!=NULL){
				// Remove the Entries
				dictionary_remove(core->hamming_queries,core->query_hamming_map[i],query_id);
				// Remove the Query
				query_map_remove(core->query_hamming_map[i],query_id);
				return EC_SUCCESS;
			}
		}
	}
	return EC_FAIL;
}

///////////////////////////////////////////////////////////////////////////////////////////////

ErrorCode MatchDocument(DocID doc_id, const char* doc_str){
	core->document = map_create();
	DocumentPtr doc = addDocument(core, doc_id);

	//deduplicate text
	String str=strdup((String)doc_str);
	dedup(str, core);
	free(str);

	//build indexes (exact match index is a map  that is already filled and up to date :) - core->document (MAP)
	
	//Create edit distance BK-Tree 
    Index_ptr edit_tree = malloc(sizeof(*edit_tree));
    build_entry_index(core->document->entry_list, MT_EDIT_DIST, edit_tree);

	//Create hamming distance BK-Trees [ one for each possible word length]
	//created an array of index trees hamming_array[num] is for the bk tree of words with num letters
	Index_ptr* hamming_array = malloc(28*sizeof(*hamming_array));
	fill_hamming_ix_array(hamming_array, core->document->entry_list, MT_HAMMING_DIST);
	

	//for every possible threshold (match_dist)
	for(int threshold=0 ; threshold<4 ; threshold++){
		
		//for every word of the edit dictionary
		for(int i=0 ; i<core->edit_queries->capacity ; i++){
            for(Entry edit_entry=core->edit_queries->array[i].entry_list->head;edit_entry!=NULL;edit_entry=edit_entry->next){
				
				EntryList results = create_entry_list();

				lookup_entry_index(edit_entry->word, edit_tree, threshold, results, MT_EDIT_DIST);
				//so now we have the results | the words of the document that matched with this threshold
				if(results->size!=0){
					
					// if it matched with at least one word
					//then we should add this word to the matched words of a query if:
					//  1) a query belongs to the th_box of the current threshold ) th_box[threshold]
					//  2) has match type edit
					//  3) belongs to the payload of the word wwe just checked
					
					for(int j=0;j<core->query_edit_map[threshold]->capacity;j++){
						QueryMapNode node=&core->query_edit_map[threshold]->array[j];
						if(node->query_list->size>0){
							for(QueryListNode lnode=node->query_list->head;lnode!=NULL;lnode=lnode->next){
								if(check_list_existence(edit_entry->payload,lnode->query->query_id)){
									matchQuery(lnode->query,edit_entry->word,doc);
								}
							}
						}
					}
				}
				destroy_entry_list(results);
			}
    	} 

		//Now the same for every word of the hammign dictionary
		//for every word of the hamming dictionary
		for(int i=0 ; i<core->hamming_queries->capacity ; i++){
            for(Entry hamming_entry=core->hamming_queries->array[i].entry_list->head;hamming_entry!=NULL;hamming_entry=hamming_entry->next){
				
				EntryList results = create_entry_list();

				lookup_entry_index(hamming_entry->word, hamming_array[strlen(hamming_entry->word)-4], threshold, results, MT_HAMMING_DIST);
				//so now we have the results | the words of the document that matched with this threshold
				if(results->size!=0){
					// if it matched with at least one word
					//then we should add this word to the matched words of a query if:
					//  1) a query belongs to the th_box of the current threshold ) th_box[threshold]
					//  2) has match type hamming
					//  3) belongs to the payload of the word wwe just checked

					for(int j=0;j<core->query_hamming_map[threshold]->capacity;j++){
						QueryMapNode node=&core->query_hamming_map[threshold]->array[j];
						if(node->query_list->size>0){
							for(QueryListNode lnode=node->query_list->head;lnode!=NULL;lnode=lnode->next){
								if(check_list_existence(hamming_entry->payload,lnode->query->query_id)){
									matchQuery(lnode->query,hamming_entry->word,doc);
								}
							}
						}
					}
				}
				destroy_entry_list(results);
			}
    	} 

		//And last but not least, take care of the exact queries
		//which appear only on threshold==0
		if(threshold==0){
			for(int i=0 ; i<core->exact_queries->capacity ; i++){
				for(Entry exact_entry=core->exact_queries->array[i].entry_list->head;exact_entry!=NULL;exact_entry=exact_entry->next){
					
					EntryList results = create_entry_list();

					bool result = map_find(core->document, exact_entry->word);
					if(result!=false){ //If there was such word in exact dictionary, there can be only one by the way
						add_entry(results, create_entry("exactresult"));
					}
					//so now we have the results | the words of the document that matched with this threshold
					if(results->size!=0){
						// if it matched with at least one word
						//then we should add this word to the matched words of a query if:
						//  1) a query belongs to the th_box of the current threshold ) th_box[threshold]
						//  2) has match type exact
						//  3) belongs to the payload of the word wwe just checked

						for(int j=0;j<core->query_exact_map->capacity;j++){
							QueryMapNode node=&core->query_exact_map->array[j];
							if(node->query_list->size>0){
								for(QueryListNode lnode=node->query_list->head;lnode!=NULL;lnode=lnode->next){
									if(check_list_existence(exact_entry->payload,lnode->query->query_id)){
										matchQuery(lnode->query,exact_entry->word,doc);
									}
								}
							}
						}
					}
					destroy_entry_list(results);
				}
			}
		}
	}

	// //Now that we are done with this document, we should clear all matched_words info from all queries so we do nto affect the next document
	clear_matchedInfo(core);

	destroy_entry_index(edit_tree);
	destroy_hamming_array(hamming_array);

	map_destroy(core->document);

	return EC_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////
int comparator (const void * p1, const void * p2)
{
  return (*(int*)p1 - *(int*)p2);
}

void merge(QueryID queryId[], int left, int mid, int right) {
    int lsubLen = mid - left + 1;
    int rsubLen = right - mid;
  
	/* Create sub arrays where:
		leftSub = queryId[0, mid] 
		rightSub = queryId[mid + 1, right] */
    int leftSub[lsubLen], rightSub[rsubLen];
    for (int i = 0; i < lsubLen; i++) leftSub[i] = queryId[left + i];
    for (int j = 0; j < rsubLen; j++) rightSub[j] = queryId[mid + 1 + j];
  
	// Merge array back until we reach the end of at least one array
    int i = 0;
    int j = 0;
    int k = left;
    while (i < lsubLen && j < rsubLen) {
        if (leftSub[i] <= rightSub[j]) {
            queryId[k] = leftSub[i];
            i++;
        }
        else {
            queryId[k] = rightSub[j];
            j++;
        }
        k++;
    }
  
	// Finally if one of the two arrays has remaining
	// queries add them to the original
    while (i < lsubLen) queryId[k++] = leftSub[i++];
    while (j < rsubLen) queryId[k++] = rightSub[j++];
}
  
// Mergesort breaks each given array into 2 parts (in the middle)
// sorting the children, and then "merging" the sorted arrays back up
void mergeSort(QueryID queryId[], int left, int right) {
    if (left < right) {
        int m = left + (right - left) / 2;
  
        mergeSort(queryId, left, m);
        mergeSort(queryId, m + 1, right);
  
        merge(queryId, left, m, right);
    }
}

ErrorCode GetNextAvailRes(DocID* p_doc_id, unsigned int* p_num_res, QueryID** p_query_ids){
	*p_doc_id=0; *p_num_res=0; *p_query_ids=0;

	if(core->document_number==0) return EC_NO_AVAIL_RES;

	*p_doc_id = core->docs[core->last_result_index]->doc_id;
	*p_num_res = core->docs[core->last_result_index]->num_res;
	mergeSort(core->docs[core->last_result_index]->query_ids, 0, core->docs[core->last_result_index]->num_res-1);
	*p_query_ids = core->docs[core->last_result_index]->query_ids;

	core->last_result_index++;
	
	return EC_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////
