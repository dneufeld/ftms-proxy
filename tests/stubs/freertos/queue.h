#pragma once
#include <vector>
#include <cstring>
struct Queue { size_t size; bool full=false; std::vector<unsigned char> data; };
using QueueHandle_t = Queue*;
#define pdTRUE 1
inline Queue* xQueueCreate(size_t, size_t size) { auto q=new Queue; q->size=size; q->data.resize(size); return q; }
inline int xQueueSend(Queue* q,const void* p,int) { if(q->full)return 0;memcpy(q->data.data(),p,q->size);q->full=true;return 1; }
inline int xQueueReceive(Queue* q,void* p,int) { if(!q->full)return 0;memcpy(p,q->data.data(),q->size);q->full=false;return 1; }
inline void xQueueReset(Queue* q) { q->full=false; }
