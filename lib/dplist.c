/**
 * \author Yongkai ZHANG
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <check.h>
#include "dplist.h"
//#define DEBUG

/*
 * definition of error codes
 */
#define DPLIST_NO_ERROR 0
#define DPLIST_MEMORY_ERROR 1   //error due to mem alloc failure
#define DPLIST_INVALID_ERROR 2  //error due to a list operation applied on a NULL list

#ifdef DEBUG
#define DEBUG_PRINTF(...) 									                                        \
        do {											                                            \
            fprintf(stderr,"\nIn %s - function %s at line %d: ", __FILE__, __func__, __LINE__);	    \
            fprintf(stderr,__VA_ARGS__);								                            \
            fflush(stderr);                                                                         \
                } while(0)
#else
#define DEBUG_PRINTF(...) (void)0
#endif


#define DPLIST_ERR_HANDLER(condition, err_code)                         \
    do {                                                                \
            if ((condition)) DEBUG_PRINTF(#condition " failed\n");      \
            assert(!(condition));                                       \
        } while(0)

/*
 * The real definition of struct list / struct node
 */

struct dplist_node
{
    dplist_node_t *prev;
    dplist_node_t *next;
    void *element;
    bool insert_copy;
};

struct dplist
{
    dplist_node_t *head;
    int number_of_element;
    void *(*element_copy)(void *src_element);
    void (*element_free)(void **element);
    int (*element_compare)(void *x, void *y);
};

dplist_t *dpl_create(void *(*element_copy)(void *src_element), void (*element_free)(void **element),
                     int (*element_compare)(void *x, void *y))
{
    dplist_t *list;
    list = malloc(sizeof(dplist_t));
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_MEMORY_ERROR);
    list->head = NULL;
    list->element_copy = element_copy;
    list->element_free = element_free;
    list->element_compare = element_compare;
    return list;
}

/** Deletes all elements in the list
 * - Every list node of the list needs to be deleted. (free memory)
 * - The list itself also needs to be deleted. (free all memory)
 * - '*list' must be set to NULL.
 * \param list a double pointer to the list
 * \param free_element if true call element_free() on the element of the list node to remove
 */
void dpl_free(dplist_t **list, bool free_element)
{
        dplist_node_t* dummylist_node=NULL;
        dplist_node_t* next=NULL;
    if (*list == NULL)
    {
        DPLIST_ERR_HANDLER(list == NULL, DPLIST_MEMORY_ERROR);
        return;
    }
    else
    {
        dummylist_node=(*list)->head; 
        while (dummylist_node!=NULL)
        {
             if (free_element && dummylist_node->insert_copy)
            {
                if(dummylist_node->element!=NULL) {
                (*list)->element_free(&(dummylist_node->element));
                }
            } 
            next=dummylist_node->next;
            free(dummylist_node);
            dummylist_node=next;
        }
        
        free(*list);
        *list = NULL;
    }
}

/** Returns the number of elements in the list.
 * - If 'list' is is NULL, -1 is returned.
 * \param list a pointer to the list
 * \return the size of the list
 */
int dpl_size(dplist_t *list)
{
    if (list == NULL )
    {
        return -1;
    }
    else
    {
        //dplist_t *dummylist = list;
        dplist_node_t *dummylist_node;
        dummylist_node = list->head;
        int size = 0;
        while (dummylist_node != NULL)
        {
            dummylist_node = dummylist_node->next;
            size++;
        }
        return size;
    }
}

/** Inserts a new list node containing an 'element' in the list at position 'index'
 * - the first list node has index 0.
 * - If 'index' is 0 or negative, the list node is inserted at the start of 'list'.
 * - If 'index' is bigger than the number of elements in the list, the list node is inserted at the end of the list.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param element a pointer to the data that needs to be inserted
 * \param index the position at which the element should be inserted in the list
 * \param insert_copy if true use element_copy() to make a copy of 'element' and use the copy in the new list node, otherwise the given element pointer is added to the list
 * \return a pointer to the list or NULL
 */
dplist_t* dpl_insert_at_index(dplist_t* list, void* element, int index, bool insert_copy) 
{
    dplist_node_t* ref_at_index; 
    dplist_node_t* list_node; 
    if (list == NULL) return NULL;
    list_node = malloc(sizeof(struct dplist_node));
    DPLIST_ERR_HANDLER(list_node == NULL, DPLIST_MEMORY_ERROR);
    list_node->insert_copy=insert_copy;
    if(insert_copy==true) {
        list_node->element = list->element_copy(element);
    }else {
        list_node->element=element;
    }
    if (list->head == NULL) { 
        list_node->prev = NULL;
        list_node->next = NULL;
        list->head = list_node;
    } else if (index <= 0) {
        list_node->prev = NULL;
        list_node->next = list->head;
        list->head->prev = list_node;
        list->head = list_node;
    } else {
        ref_at_index = dpl_get_reference_at_index(list, index);
        assert(ref_at_index != NULL);
        if (index < dpl_size(list)) { 
            list_node->prev = ref_at_index->prev;
            list_node->next = ref_at_index;
            ref_at_index->prev->next = list_node;
            ref_at_index->prev = list_node;
        } else { 
            assert(ref_at_index->next == NULL);
            list_node->next = NULL;
            list_node->prev = ref_at_index;
            ref_at_index->next = list_node;
        }
    }
    return list;
}

/** Removes the list node at index 'index' from the list.
 * - The list node itself should always be freed.
 * - If 'index' is 0 or negative, the first list node is removed.
 * - If 'index' is bigger than the number of elements in the list, the last list node is removed.
 * - If the list is empty, return the unmodified list.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param index the position at which the node should be removed from the list
 * \param free_element if true, call element_free() on the element of the list node to remove
 * \return a pointer to the list or NULL
 */
dplist_t *dpl_remove_at_index(dplist_t *list, int index, bool free_element)
{
    dplist_node_t *ref_at_index;
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_INVALID_ERROR);
    if (list == NULL)
    {
        return NULL;
    }
    else if (list->head == NULL)
    {
        return list;
    }
    else if (index <= 0)
    {
        ref_at_index = list->head;
        if (ref_at_index->next != NULL)
        {
            list->head = ref_at_index->next;
            ref_at_index->next->prev = NULL;
        }
        else
        {
            list->head = NULL;
        }
    }
    else
    {
        ref_at_index = dpl_get_reference_at_index(list, index);
        assert(ref_at_index != NULL);
        if (index < dpl_size(list) - 1)
        {
            ref_at_index->prev->next = ref_at_index->next;
            ref_at_index->next->prev = ref_at_index->prev;
        }
        else
        {
            assert(ref_at_index->next == NULL);
            if (ref_at_index->prev != NULL)
            {
                ref_at_index->prev->next = NULL;
            }
            else
            {
                list->head = NULL;
            }
        }
    }
    if (free_element == true && ref_at_index->insert_copy)
        {
            list->element_free(&ref_at_index->element);
        }
    free(ref_at_index);
    return list;
}

/** Returns a reference to the list node with index 'index' in the list.
 * - If 'index' is 0 or negative, a reference to the first list node is returned.
 * - If 'index' is bigger than the number of list nodes in the list, a reference to the last list node is returned.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param index the position of the node for which the reference is returned
 * \return a pointer to the list node at the given index or NULL
 */
dplist_node_t *dpl_get_reference_at_index(dplist_t *list, int index)
{
    dplist_node_t *dummy;
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_INVALID_ERROR);
    if (list->head == NULL)
        return NULL;
    if (index <= 0)
    {
        return list->head;
    }
    else
    {
        int count;
        for (dummy = list->head, count = 0; dummy->next != NULL; dummy = dummy->next, count++)
        {
            if (count == index)
            {
                return dummy;
            }
        }
    }
    return dummy;
}

/** Returns the list element contained in the list node with index 'index' in the list.
 * - return is not returning a copy of the element with index 'index', i.e. 'element_copy()' is not used.
 * - If 'index' is 0 or negative, the element of the first list node is returned.
 * - If 'index' is bigger than the number of elements in the list, the element of the last list node is returned.
 * - If the list is empty, NULL is returned.
 * - If 'list' is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param index the position of the node for which the element is returned
 * \return a pointer to the element at the given index or NULL
 */
void *dpl_get_element_at_index(dplist_t *list, int index)
{
    // int count;
    // dplist_node_t *dummy;
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_INVALID_ERROR);
    if (list->head == NULL)
    {
        return NULL;
    }
    dplist_node_t *dummy;
    dummy = dpl_get_reference_at_index(list, index);
    return dummy->element;
}

/** Returns an index to the first list node in the list containing 'element'.
 * - the first list node has index 0.
 * - Use 'element_compare()' to search 'element' in the list, a match is found when 'element_compare()' returns 0.
 * - If 'element' is not found in the list, -1 is returned.
 * - If 'list' is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param element the element to look for
 * \return the index of the element that matches 'element'
 */
int dpl_get_index_of_element(dplist_t *list, void *element)
{
    int count;
    dplist_node_t *dummy;
    DPLIST_ERR_HANDLER(list == NULL, DPLIST_INVALID_ERROR);
    if (list->head == NULL)
    {
        return -1;
    }
    for (dummy = list->head, count = 0; dummy != NULL; dummy = dummy->next, count++)
    {
        if (list->element_compare(element, dummy->element) == 0)
        {
            return count;
        }
    }
    return -1;
}

/** Returns the element contained in the list node with reference 'reference' in the list.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * - If 'reference' is NULL, NULL is returned.
 * - If 'reference' is not an existing reference in the list, NULL is returned.
 * \param list a pointer to the list
 * \param reference a pointer to a certain node in the list
 * \return the element contained in the list node or NULL
 */
void *dpl_get_element_at_reference(dplist_t *list, dplist_node_t *reference)
{
    //dplist_node_t *list_node;
    if (list == NULL || list->head == NULL || reference == NULL)
        return NULL;
    for (int i=0; i<dpl_size(list);i++)
    {
        if (dpl_get_reference_at_index(list,i) == reference)
        {
            return reference->element;
        }
    }
    return NULL;
}

//*** HERE STARTS THE EXTRA SET OF OPERATORS ***//

/** Returns a reference to the first list node of the list.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \return a reference to the first list node of the list or NULL
 */
dplist_node_t *dpl_get_first_reference(dplist_t *list)
{
    if (list == NULL || list->head == NULL)
        return NULL;
    return list->head;
}

/** Returns a reference to the last list node of the list.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \return a reference to the last list node of the list or NULL
 */
dplist_node_t *dpl_get_last_reference(dplist_t *list)
{
    if (list == NULL || list->head == NULL)
        return NULL;
    int size = dpl_size(list);
    return dpl_get_reference_at_index(list, size - 1);
}

/** Returns the index of the list node in the list with reference 'reference'.
 * - the first list node has index 0.
 * - If the list is empty, -1 is returned.
 * - If 'list' is is NULL, -1 is returned.
 * - If 'reference' is NULL, -1 returned.
 * - If 'reference' is not an existing reference in the list, -1 is returned.
 * \param list a pointer to the list
 * \param reference a pointer to a certain node in the list
 * \return the index of the given reference in the list
 */
int dpl_get_index_of_reference(dplist_t *list,const dplist_node_t *reference)
{
    if (list == NULL || list->head==NULL || reference==NULL) return -1;
    for(int i=0;i<dpl_size(list);i++)
    {
        if(dpl_get_reference_at_index(list,i)==reference) return i;
    }

    return -1;
}

/** Returns a reference to the next list node of the list node with reference 'reference' in the list.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * - If 'reference' is NULL, NULL is returned.
 * - If 'reference' is not an existing reference in the list, NULL is returned.
 * \param list a pointer to the list
 * \param reference a pointer to a certain node in the list
 * \return a pointer to the node next to 'reference' in the list or NULL
 */
dplist_node_t *dpl_get_next_reference(dplist_t *list, dplist_node_t *reference)
{
    if (list == NULL || list->head==NULL || reference==NULL) return NULL;
    int next_index = dpl_get_index_of_reference(list, reference);
    if (dpl_get_reference_at_index(list, next_index + 1) == reference)
    {
        return NULL;
    }
    else
    {
        return dpl_get_reference_at_index(list, next_index + 1);
    }
}

/** Returns a reference to the previous list node of the list node with reference 'reference' in 'list'.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * - If 'reference' is NULL, NULL is returned.
 * - If 'reference' is not an existing reference in the list, NULL is returned.
 * \param list a pointer to the list
 * \param reference a pointer to a certain node in the list
 * \return pointer to the node previous to 'reference' in the list or NULL
 */
dplist_node_t *dpl_get_previous_reference(dplist_t *list, dplist_node_t *reference)
{
    if (list == NULL || reference == NULL || list->head == NULL) return NULL;
   
    int next_index = dpl_get_index_of_reference(list, reference);
    if (dpl_get_reference_at_index(list, next_index - 1) == list->head)
    {
        if (next_index==1){
            return list->head;

        }
        return NULL;
    }
    else
    {
        return dpl_get_reference_at_index(list, next_index - 1);
    }
}

/** Returns a reference to the first list node in the list containing 'element'.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * - If 'element' is not found in the list, NULL is returned.
 * \param list a pointer to the list
 * \param element a pointer to an element
 * \return the first list node in the list containing 'element' or NULL
 */
dplist_node_t *dpl_get_reference_of_element(dplist_t *list, void *element)
{
    if (list == NULL || list->head==NULL) return NULL;
    int index = dpl_get_index_of_element(list, element);
    if (index == -1)
    {
        return NULL;
    }
    else
    {
        return dpl_get_reference_at_index(list, index);
    }
}


/** Inserts a new list node containing an 'element' in the list at position 'reference'.
 * - If 'list' is is NULL, NULL is returned.
 * - If 'reference' is NULL, NULL is returned (nothing is inserted).
 * - If 'reference' is not an existing reference in the list, 'list' is returned (nothing is inserted).
 * \param list a pointer to the list
 * \param element a pointer to an element
 * \param reference a pointer to a certain node in the list
 * \param insert_copy if true use element_copy() to make a copy of 'element' and use the copy in the new list node, otherwise the given element pointer is added to the list
 * \return a pointer to the list or NULL
 */
dplist_t *dpl_insert_at_reference(dplist_t *list, void *element, dplist_node_t *reference, bool insert_copy)
{
    // Check for invalid inputs
    if (list == NULL || reference == NULL)
        return NULL;

    // Create a new list node
    dplist_node_t *list_node = malloc(sizeof(dplist_node_t));
    DPLIST_ERR_HANDLER(list_node == NULL, DPLIST_MEMORY_ERROR);
    list_node->insert_copy = insert_copy;

    // Copy or use the given element
    list_node->element = (insert_copy ? list->element_copy(element) : element);

    // Insert the new node before the reference
    if (reference->prev != NULL)
    {
        // Insert in the middle of the list
        list_node->prev = reference->prev;
        reference->prev->next = list_node;
    }
    else
    {
        // Insert at the beginning of the list
        list->head = list_node;
        list_node->prev = NULL;
    }

    // Common step: Set the next pointer for the new node and the reference node
    list_node->next = reference;
    reference->prev = list_node;

    return list; // Return the modified list
}


/** Inserts a new list node containing 'element' in the sorted list and returns a pointer to the new list.
 * - The list must be sorted or empty before calling this function.
 * - The sorting is done in ascending order according to a comparison function.
 * - If two members compare as equal, their order in the sorted array is undefined.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param element a pointer to an element
 * \param insert_copy if true use element_copy() to make a copy of 'element' and use the copy in the new list node, otherwise the given element pointer is added to the list
 * \return a pointer to the list or NULL
 */
dplist_t *dpl_insert_sorted(dplist_t *list, void *element, bool insert_copy)
{
    if (list == NULL) {
        return NULL;
    }

    dplist_node_t *new_node = malloc(sizeof(dplist_node_t));
    DPLIST_ERR_HANDLER(new_node == NULL, DPLIST_MEMORY_ERROR);

    new_node->insert_copy = insert_copy;
    new_node->element = (insert_copy) ? list->element_copy(element) : element;

    dplist_node_t *current = list->head;

    // Special case: insert at the beginning
    if (current == NULL || list->element_compare(element, current->element) <= 0) {
        new_node->prev = NULL;
        new_node->next = current;
        if (current != NULL) {
            current->prev = new_node;
        }
        list->head = new_node;
        return list;
    }

    // Find the insertion point
    while (current->next != NULL && list->element_compare(element, current->next->element) > 0) {
        current = current->next;
    }

    // Insert after 'current'
    new_node->prev = current;
    new_node->next = current->next;
    if (current->next != NULL) {
        current->next->prev = new_node;
    }
    current->next = new_node;

    return list;
}


/** Removes the list node with reference 'reference' in the list.
 * - The list node itself should always be freed.
 * - If 'reference' is NULL, NULL is returned (nothing is removed).
 * - If 'reference' is not an existing reference in the list, 'list' is returned (nothing is removed).
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param reference a pointer to a certain node in the list
 * \param free_element if true call element_free() on the element of the list node to remove
 * \return a pointer to the list or NULL
 */
dplist_t *dpl_remove_at_reference(dplist_t *list, dplist_node_t *reference, bool free_element)
{
    if (list == NULL || reference==NULL) return NULL;
    if (dpl_get_index_of_reference(list, reference) == -1)
    {
        return list;
    }
    else
    {
        if (reference->prev == NULL)
        {
            list->head = reference->next;
            reference->next->prev = NULL;
        }
        else if (reference->next == NULL)
        {
            reference->prev->next = NULL;
        }
        else
        {
            reference->prev->next = reference->next;
            reference->next->prev = reference->prev;
        }
        if (free_element == true && reference->insert_copy)
        {
            list->element_free(&reference->element);
        }
        free(reference);
    }
    return list;
}

/** Finds the first list node in the list that contains 'element' and removes the list node from 'list'.
 * - If 'element' is not found in 'list', the unmodified 'list' is returned.
 * - If 'list' is is NULL, NULL is returned.
 * \param list a pointer to the list
 * \param element a pointer to an element
 * \param free_element if true call element_free() on the element of the list node to remove
 * \return a pointer to the list or NULL
 */
dplist_t *dpl_remove_element(dplist_t *list, void *element, bool free_element)
{
    if (list == NULL || element == NULL) {
        // Return early if the list or element is NULL
        return list;
    }

    // Find the node containing the element
    dplist_node_t *list_node = list->head;
    while (list_node != NULL) {
        if (list->element_compare(list_node->element, element) == 0) {
            // Element found in the list
            if (list_node->prev == NULL) {
                // Element is at the head
                list->head = list_node->next;
            } else {
                list_node->prev->next = list_node->next;
            }

            if (list_node->next != NULL) {
                list_node->next->prev = list_node->prev;
            }

            if (free_element && list_node->insert_copy) {
                // Free the element if requested and if it's an inserted copy
                list->element_free(&list_node->element);
            }

            free(list_node); // Free the node
            return list;
        }

        list_node = list_node->next;
    }

    // Element not found in the list
    return list;
}

