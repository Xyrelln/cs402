#include "my402list.h"
#include <stdlib.h>

int My402ListLength(My402List *list)
{
    return list->num_members;
}

int My402ListEmpty(My402List *list)
{
    return list->num_members == 0;
}

int My402ListAppend(My402List *list, void *obj)
{
    My402ListElem *pNew = (My402ListElem *)malloc(sizeof(My402ListElem));
    if (pNew == 0)
        return 1;

    pNew->obj = obj;
    pNew->next = &(list->anchor);
    pNew->prev = list->anchor.prev;
    list->anchor.prev->next = pNew;
    list->anchor.prev = pNew;

    list->num_members++;
    return 0;
}

int My402ListPrepend(My402List *list, void *obj)
{
    My402ListElem *pNew = (My402ListElem *)malloc(sizeof(My402ListElem));
    if (pNew == 0)
        return 1;

    pNew->obj = obj;
    pNew->next = list->anchor.next;
    pNew->prev = &(list->anchor);
    list->anchor.next = pNew;
    pNew->next->prev = pNew;

    list->num_members++;
    return 0;
}

void My402ListUnlink(My402List *list, My402ListElem *elem)
{
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    free(elem);
    list->num_members--;
}

void My402ListUnlinkAll(My402List *list)
{
    My402ListElem *pCurr = list->anchor.next;
    while (!My402ListEmpty(list))
    {
        pCurr = pCurr->next;
        free(pCurr->prev);
        list->num_members -= 1;
    }

    list->anchor.next = NULL;
    list->anchor.prev = NULL;

    return;
}

int My402ListInsertAfter(My402List *list, void *obj, My402ListElem *elem)
{
    My402ListElem *pNew = malloc(sizeof(My402ListElem));
    if (pNew == NULL)
        return 1;

    My402ListElem *target = list->anchor.next;
    while (target != elem && target != NULL)
        target = My402ListNext(list, target);

    pNew->obj = obj;
    pNew->next = target->next;
    pNew->prev = target;
    target->next->prev = pNew;
    target->next = pNew;
    list->num_members++;

    return 0;
}

int My402ListInsertBefore(My402List *list, void *obj, My402ListElem *elem)
{
    My402ListElem *pNew = malloc(sizeof(My402ListElem));
    if (pNew == NULL)
        return 1;

    My402ListElem *target = &(list->anchor);
    while (target != elem && target != NULL)
        target = My402ListNext(list, target);

    pNew->obj = obj;
    pNew->next = target;
    pNew->prev = target->prev;
    target->prev->next = pNew;
    target->prev = pNew;
    list->num_members++;

    return 0;
}

My402ListElem *My402ListFirst(My402List *list)
{
    if (list->Empty(list))
        return NULL;
    return list->anchor.next;
}

My402ListElem *My402ListLast(My402List *list)
{
    if (list->Empty(list))
        return NULL;
    return list->anchor.prev;
}

My402ListElem *My402ListNext(My402List *list, My402ListElem *elem)
{
    return elem == list->anchor.prev ? NULL : elem->next;
}

My402ListElem *My402ListPrev(My402List *list, My402ListElem *elem)
{
    return elem == list->anchor.next ? NULL : elem->prev;
}

My402ListElem *My402ListFind(My402List *list, void *obj)
{
    My402ListElem *pFinder = list->anchor.next;
    while (pFinder != NULL && pFinder->obj != obj)
    {
        pFinder = My402ListNext(list, pFinder);
    }
    return pFinder;
}

int My402ListInit(My402List *list)
{
    list->num_members = 0;

    list->anchor.next = &list->anchor;
    list->anchor.prev = &list->anchor;
    list->anchor.obj = NULL;

    list->Length = My402ListLength;
    list->Empty = My402ListEmpty;
    list->Append = My402ListAppend;
    list->Prepend = My402ListPrepend;
    list->Unlink = My402ListUnlink;
    list->UnlinkAll = My402ListUnlinkAll;
    list->InsertBefore = My402ListInsertBefore;
    list->InsertAfter = My402ListInsertAfter;
    list->First = My402ListFirst;
    list->Last = My402ListLast;
    list->Next = My402ListNext;
    list->Prev = My402ListPrev;
    list->Find = My402ListFind;

    return 0;
}
