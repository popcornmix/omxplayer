/*
 * OMX IL Alsa Sink component
 * Copyright (c) 2016 Timo Teräs
 *
 * This Program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * TODO:
 * - timeouts for state transition failures
 */

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Broadcom.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

struct _GOMX_COMMAND;
struct _GOMX_PORT;
struct _GOMX_COMPONENT;

template <class X> static inline X max(X a, X b)
{
	return (a > b) ? a : b;
}

template <class X> static OMX_ERRORTYPE omx_cast(X* &toptr, OMX_PTR fromptr)
{
	toptr = (X*) fromptr;
	if (toptr->nSize < sizeof(X)) return OMX_ErrorBadParameter;
	if (toptr->nVersion.nVersion != OMX_VERSION) return OMX_ErrorVersionMismatch;
	return OMX_ErrorNone;
}

template <class X> static void omx_init(X &omx)
{
	omx.nSize = sizeof(X);
	omx.nVersion.nVersion = OMX_VERSION;
}

#if 1
#include <utils/log.h>
#define CLOG(notice, comp, port, msg, ...) do { \
	struct _GOMX_PORT *_port = (struct _GOMX_PORT *) port; \
	if (_port) CLog::Log(notice ? LOGNOTICE : LOGDEBUG, "[%p port %d]: %s: " msg "\n", comp, _port->def.nPortIndex, __func__ , ##__VA_ARGS__); \
	else CLog::Log(notice ? LOGNOTICE : LOGDEBUG, "[%p] %s: " msg "\n", comp, __func__ , ##__VA_ARGS__); \
} while (0)
#else
#define CLOG(notice, comp, port, msg, ...) do { \
	struct _GOMX_PORT *_port = (struct _GOMX_PORT *) port; \
	if (_port) fprintf(stderr, "[%p port %d]: %s: " msg "\n", comp, _port->def.nPortIndex, __func__ , ##__VA_ARGS__); \
	else fprintf(stderr, "[%p] %s: " msg "\n", comp, __func__ , ##__VA_ARGS__); \
} while (0)
#endif

#define CINFO(comp, port, msg, ...) CLOG(1, comp, port, msg , ##__VA_ARGS__)
#define CDEBUG(comp, port, msg, ...) CLOG(0, comp, port, msg , ##__VA_ARGS__)

/* Generic OMX helpers */

typedef struct _GOMX_QUEUE {
	void *head, *tail;
	ptrdiff_t offset;
	size_t num;
} GOMX_QUEUE;

static void gomxq_init(GOMX_QUEUE *q, ptrdiff_t offset)
{
	q->head = q->tail = 0;
	q->offset = offset;
	q->num = 0;
}

static void **gomxq_nextptr(GOMX_QUEUE *q, void *item)
{
	return (void**) ((uint8_t*)item + q->offset);
}

static void gomxq_enqueue(GOMX_QUEUE *q, void *item)
{
	*gomxq_nextptr(q, item) = 0;
	if (q->tail) {
		*gomxq_nextptr(q, q->tail) = item;
		q->tail = item;
	} else {
		q->head = q->tail = item;
	}
	q->num++;
}

static void *gomxq_dequeue(GOMX_QUEUE *q)
{
	void *item = q->head;
	if (item) {
		q->head = *gomxq_nextptr(q, item);
		if (!q->head) q->tail = 0;
		q->num--;
	}
	return item;
}

typedef struct _GOMX_COMMAND {
	void *next;
	OMX_COMMANDTYPE cmd;
	OMX_U32 param;
	OMX_PTR data;
} GOMX_COMMAND;

typedef struct _GOMX_PORT {
	OMX_BOOL new_enabled;
	OMX_PARAM_PORTDEFINITIONTYPE def;

	size_t num_buffers, num_buffers_old;
	pthread_cond_t cond_no_buffers;
	pthread_cond_t cond_populated;
	pthread_cond_t cond_idle;

	OMX_HANDLETYPE tunnel_comp;
	OMX_U32 tunnel_port;
	bool tunnel_supplier;
	GOMX_QUEUE tunnel_supplierq;

	OMX_ERRORTYPE (*do_buffer)(struct _GOMX_COMPONENT *, struct _GOMX_PORT *, OMX_BUFFERHEADERTYPE *);
	OMX_ERRORTYPE (*flush)(struct _GOMX_COMPONENT *, struct _GOMX_PORT *);
} GOMX_PORT;

typedef struct _GOMX_COMPONENT {
	OMX_COMPONENTTYPE omx;
	OMX_CALLBACKTYPE cb;
	OMX_STATETYPE state, wanted_state;

	pthread_t component_thread, worker_thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	const char *name;
	size_t nports;
	GOMX_PORT *ports;
	GOMX_QUEUE cmdq;

	void* (*worker)(void *);
	OMX_ERRORTYPE (*statechange)(struct _GOMX_COMPONENT *);
} GOMX_COMPONENT;

static GOMX_PORT *gomx_get_port(GOMX_COMPONENT *comp, size_t idx)
{
	if (idx < 0 || idx >= comp->nports) return 0;
	return &comp->ports[idx];
}

OMX_ERRORTYPE gomx_get_component_version(
		OMX_HANDLETYPE hComponent, OMX_STRING pComponentName,
		OMX_VERSIONTYPE *pComponentVersion, OMX_VERSIONTYPE *pSpecVersion, OMX_UUIDTYPE *pComponentUUID)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	CDEBUG(comp, 0, "enter");
	strcpy(pComponentName, comp->name);
	pComponentVersion->nVersion = OMX_VERSION;
	pSpecVersion->nVersion = OMX_VERSION;
	memcpy(pComponentUUID, &hComponent, sizeof hComponent);
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE gomx_get_parameter(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nParamIndex, OMX_PTR pComponentParameterStructure)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	GOMX_PORT *port;
	OMX_PORT_PARAM_TYPE *ppt;
	OMX_PARAM_PORTDEFINITIONTYPE *pdt;
	OMX_ERRORTYPE r;
	OMX_PORTDOMAINTYPE domain;

	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;

	CDEBUG(comp, 0, "called %x, %p", nParamIndex, pComponentParameterStructure);
	switch (nParamIndex) {
	case OMX_IndexParamAudioInit:
		domain = OMX_PortDomainAudio;
		goto param_init;
	case OMX_IndexParamVideoInit:
		domain = OMX_PortDomainVideo;
		goto param_init;
	case OMX_IndexParamImageInit:
		domain = OMX_PortDomainImage;
		goto param_init;
	case OMX_IndexParamOtherInit:
		domain = OMX_PortDomainOther;
		goto param_init;
	param_init:
		if ((r = omx_cast(ppt, pComponentParameterStructure))) return r;
		ppt->nPorts = 0;
		ppt->nStartPortNumber = 0;
		for (size_t i = 0; i < comp->nports; i++) {
			if (comp->ports[i].def.eDomain != domain)
				continue;
			if (!ppt->nPorts)
				ppt->nStartPortNumber = i;
			ppt->nPorts++;
		}
		break;
	case OMX_IndexParamPortDefinition:
		if ((r = omx_cast(pdt, pComponentParameterStructure))) return r;
		if (!(port = gomx_get_port(comp, pdt->nPortIndex))) return OMX_ErrorBadPortIndex;
		memcpy(pComponentParameterStructure, &port->def, sizeof *pdt);
		break;
	default:
		CINFO(comp, 0, "UNSUPPORTED %x, %p", nParamIndex, pComponentParameterStructure);
		return OMX_ErrorNotImplemented;
	}
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE gomx_get_state(OMX_HANDLETYPE hComponent, OMX_STATETYPE *pState)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	*pState = comp->state;
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE gomx_component_tunnel_request(
		OMX_HANDLETYPE hComponent, OMX_U32 nPort,
		OMX_HANDLETYPE hTunneledComp, OMX_U32 nTunneledPort, OMX_TUNNELSETUPTYPE* pTunnelSetup)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	GOMX_PORT *port = 0;

	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;
	if (!(port = gomx_get_port(comp, nPort))) return OMX_ErrorBadPortIndex;
	if (comp->state != OMX_StateLoaded && port->def.bEnabled)
		return OMX_ErrorIncorrectStateOperation;

	if (hTunneledComp == 0 || pTunnelSetup == 0) {
		port->tunnel_comp = 0;
		return OMX_ErrorNone;
	}

	if (port->def.eDir == OMX_DirInput) {
		/* Negotiate parameters */
		OMX_PARAM_PORTDEFINITIONTYPE param;
		omx_init(param);
		param.nPortIndex = nTunneledPort;
		if (OMX_GetParameter(hTunneledComp, OMX_IndexParamPortDefinition, &param))
			goto not_compatible;
		if (param.eDomain != port->def.eDomain)
			goto not_compatible;

		param.nBufferCountActual = max(param.nBufferCountMin, port->def.nBufferCountMin);
		param.nBufferSize = max(port->def.nBufferSize, param.nBufferSize);
		param.nBufferAlignment = max(port->def.nBufferAlignment, param.nBufferAlignment);
		port->def.nBufferCountActual = param.nBufferCountActual;
		port->def.nBufferSize = param.nBufferSize;
		port->def.nBufferAlignment = param.nBufferAlignment;
		if (OMX_SetParameter(hTunneledComp, OMX_IndexParamPortDefinition, &param))
			goto not_compatible;

		/* Negotiate buffer supplier */
		OMX_PARAM_BUFFERSUPPLIERTYPE suppl;
		omx_init(suppl);
		suppl.nPortIndex = nTunneledPort;
		if (OMX_GetParameter(hTunneledComp, OMX_IndexParamCompBufferSupplier, &suppl))
			goto not_compatible;

		/* Being supplier is not supported so ask the other side to be it */
		suppl.eBufferSupplier =
			(pTunnelSetup->eSupplier == OMX_BufferSupplyOutput)
			? OMX_BufferSupplyOutput : OMX_BufferSupplyInput;
		if (OMX_SetParameter(hTunneledComp, OMX_IndexParamCompBufferSupplier, &suppl))
			goto not_compatible;

		port->tunnel_comp = hTunneledComp;
		port->tunnel_port = nTunneledPort;
		port->tunnel_supplier = (suppl.eBufferSupplier == OMX_BufferSupplyInput);
		pTunnelSetup->eSupplier = suppl.eBufferSupplier;
		CINFO(comp, port, "ComponentTunnnelRequest: %p %d", hTunneledComp, nTunneledPort);
	} else {
		CINFO(comp, port, "OUTPUT TUNNEL UNSUPPORTED: %p, %d, %p", hTunneledComp, nTunneledPort, pTunnelSetup);
		return OMX_ErrorNotImplemented;
	}
	return OMX_ErrorNone;

not_compatible:
	CINFO(comp, port, "ComponentTunnnelRequest: %p %d - NOT COMPATIBLE", hTunneledComp, nTunneledPort);
	return OMX_ErrorPortsNotCompatible;
}

static void __gomx_event(GOMX_COMPONENT *comp, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
	if (!comp->cb.EventHandler) return;
	pthread_mutex_unlock(&comp->mutex);
	comp->cb.EventHandler((OMX_HANDLETYPE) comp, comp->omx.pApplicationPrivate, eEvent, nData1, nData2, pEventData);
	pthread_mutex_lock(&comp->mutex);
}

static void __gomx_port_update_buffer_state(GOMX_COMPONENT *comp, GOMX_PORT *port)
{
	if (port->num_buffers_old == port->num_buffers)
		return;

	port->def.bPopulated = (port->num_buffers >= port->def.nBufferCountActual) ? OMX_TRUE : OMX_FALSE;
	if (port->num_buffers == 0)
		pthread_cond_signal(&port->cond_no_buffers);
	else if (port->num_buffers == port->def.nBufferCountActual)
		pthread_cond_signal(&port->cond_populated);
}

static OMX_ERRORTYPE gomx_use_buffer(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE **ppBufferHdr,
				     OMX_U32 nPortIndex, OMX_PTR pAppPrivate, OMX_U32 nSizeBytes, OMX_U8* pBuffer)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	OMX_BUFFERHEADERTYPE *hdr;
	GOMX_PORT *port;
	void *buf;

	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;
	if (!(port = gomx_get_port(comp, nPortIndex))) return OMX_ErrorBadPortIndex;

	if (!((comp->state == OMX_StateLoaded && comp->wanted_state == OMX_StateIdle) ||
	      (port->def.bEnabled == OMX_FALSE &&
			(comp->state == OMX_StateExecuting ||
			 comp->state == OMX_StatePause ||
			 comp->state == OMX_StateIdle))))
		return OMX_ErrorIncorrectStateOperation;

	buf = malloc(sizeof(OMX_BUFFERHEADERTYPE) + (pBuffer ? 0 : nSizeBytes));
	if (!buf) return OMX_ErrorInsufficientResources;

	hdr = (OMX_BUFFERHEADERTYPE *) buf;
	memset(hdr, 0, sizeof *hdr);
	omx_init(*hdr);
	hdr->pBuffer = pBuffer ? (OMX_U8*)pBuffer : (OMX_U8*)((char*)buf + nSizeBytes);
	hdr->nAllocLen = nSizeBytes;
	hdr->pAppPrivate = pAppPrivate;
	if (port->def.eDir == OMX_DirInput) {
		hdr->nInputPortIndex = nPortIndex;
		hdr->pOutputPortPrivate = pAppPrivate;
	} else {
		hdr->nOutputPortIndex = nPortIndex;
		hdr->pInputPortPrivate = pAppPrivate;
	}
	pthread_mutex_lock(&comp->mutex);
	port->num_buffers++;
	__gomx_port_update_buffer_state(comp, port);
	pthread_mutex_unlock(&comp->mutex);

	CDEBUG(comp, port, "allocated: %d, %p, %u, %p", nPortIndex, pAppPrivate, nSizeBytes, pBuffer);
	*ppBufferHdr = hdr;

	return OMX_ErrorNone;
}

static OMX_ERRORTYPE gomx_allocate_buffer(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE **ppBufferHdr,
					  OMX_U32 nPortIndex, OMX_PTR pAppPrivate, OMX_U32 nSizeBytes)
{
	return gomx_use_buffer(hComponent, ppBufferHdr, nPortIndex, pAppPrivate, nSizeBytes, 0);
}

static OMX_ERRORTYPE gomx_free_buffer(OMX_HANDLETYPE hComponent, OMX_U32 nPortIndex, OMX_BUFFERHEADERTYPE* pBuffer)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	GOMX_PORT *port;

	if (!(port = gomx_get_port(comp, nPortIndex))) return OMX_ErrorBadPortIndex;

	/* Freeing buffer is allowed in all states, so destructor can
	 * synchronize successfully. */

	pthread_mutex_lock(&comp->mutex);

	if (!((comp->state == OMX_StateIdle && comp->wanted_state == OMX_StateLoaded) ||
	      (port->def.bEnabled == OMX_FALSE &&
			(comp->state == OMX_StateExecuting ||
			 comp->state == OMX_StatePause ||
			 comp->state == OMX_StateIdle)))) {
		/* In unexpected states the port unpopulated error is sent. */
		if (port->num_buffers == port->def.nBufferCountActual)
			__gomx_event(comp, OMX_EventError, OMX_ErrorPortUnpopulated, nPortIndex, 0);
		/* FIXME? should we mark the port also down */
	}

	port->num_buffers--;
	__gomx_port_update_buffer_state(comp, port);

	pthread_mutex_unlock(&comp->mutex);

	free(pBuffer);

	return OMX_ErrorNone;
}

static void __gomx_port_queue_supplier_buffer(GOMX_PORT *port, OMX_BUFFERHEADERTYPE *hdr)
{
	gomxq_enqueue(&port->tunnel_supplierq, (void *) hdr);
	if (port->tunnel_supplierq.num == port->num_buffers)
		pthread_cond_broadcast(&port->cond_idle);
}

static OMX_ERRORTYPE __gomx_empty_buffer_done(GOMX_COMPONENT *comp, OMX_BUFFERHEADERTYPE *hdr)
{
	GOMX_PORT *port = gomx_get_port(comp, hdr->nInputPortIndex);
	OMX_ERRORTYPE r;

	if (port->tunnel_comp) {
		/* Buffers are sent to the tunneled port once emptied as long as
		 * the component is in the OMX_StateExecuting state */
		if ((comp->state == OMX_StateExecuting && port->def.bEnabled) ||
		    !port->tunnel_supplier) {
			pthread_mutex_unlock(&comp->mutex);
			r = OMX_FillThisBuffer(port->tunnel_comp, hdr);
			pthread_mutex_lock(&comp->mutex);
		} else {
			r = OMX_ErrorIncorrectStateOperation;
		}
	} else {
		pthread_mutex_unlock(&comp->mutex);
		r = comp->cb.EmptyBufferDone((OMX_HANDLETYPE) comp, hdr->pAppPrivate, hdr);
		pthread_mutex_lock(&comp->mutex);
	}

	if (r != OMX_ErrorNone && port->tunnel_supplier) {
		__gomx_port_queue_supplier_buffer(port, hdr);
		r = OMX_ErrorNone;
	}

	return r;
}

static OMX_ERRORTYPE gomx_empty_this_buffer(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	GOMX_PORT *port;
	OMX_ERRORTYPE r;

	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;
	if (comp->state != OMX_StatePause && comp->state != OMX_StateExecuting &&
	    comp->wanted_state != OMX_StateExecuting)
		return OMX_ErrorIncorrectStateOperation;

	if (!(port = gomx_get_port(comp, pBuffer->nInputPortIndex)))
		return OMX_ErrorBadPortIndex;

	pthread_mutex_lock(&comp->mutex);
	if (port->def.bEnabled) {
		if (port->do_buffer)
			r = port->do_buffer(comp, port, pBuffer);
		else
			r = __gomx_empty_buffer_done(comp, pBuffer);
	} else {
		if (port->tunnel_supplier) {
			__gomx_port_queue_supplier_buffer(port, pBuffer);
			r = OMX_ErrorNone;
		} else {
			r = OMX_ErrorIncorrectStateOperation;
		}
	}
	pthread_mutex_unlock(&comp->mutex);
	return r;
}

static OMX_ERRORTYPE gomx_fill_this_buffer(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer)
{
	CDEBUG(hComponent, 0, "stub");
	return OMX_ErrorNotImplemented;
}

static void __gomx_process_mark(GOMX_COMPONENT *comp, OMX_BUFFERHEADERTYPE *hdr)
{
	if (hdr->hMarkTargetComponent == (OMX_HANDLETYPE) comp) {
		__gomx_event(comp, OMX_EventMark, 0, 0, hdr->pMarkData);
		hdr->hMarkTargetComponent = 0;
		hdr->pMarkData = 0;
	}
}

static OMX_ERRORTYPE __gomx_port_unpopulate(GOMX_COMPONENT *comp, GOMX_PORT *port)
{
	OMX_BUFFERHEADERTYPE *hdr;
	void *buf;

	if (port->tunnel_supplier) {
		CINFO(comp, port, "waiting for supplier buffers (%d / %d)",
			(int)port->tunnel_supplierq.num, (int)port->num_buffers);
		while (port->tunnel_supplierq.num != port->num_buffers)
			pthread_cond_wait(&port->cond_idle, &comp->mutex);

		CINFO(comp, port, "free tunnel buffers");
		while ((hdr = (OMX_BUFFERHEADERTYPE*)gomxq_dequeue(&port->tunnel_supplierq)) != 0) {
			buf = hdr->pBuffer;
			OMX_FreeBuffer(port->tunnel_comp, port->tunnel_port, hdr);
			free(buf);
			port->num_buffers--;
			__gomx_port_update_buffer_state(comp, port);
		}
	} else {
		/* Wait client / tunnel supplier to allocate buffers */
		CINFO(comp, port, "waiting %d buffers to be freed", (int)port->num_buffers);
		while (port->num_buffers > 0)
			pthread_cond_wait(&port->cond_no_buffers, &comp->mutex);
	}

	CINFO(comp, port, "UNPOPULATED");
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE __gomx_port_populate(GOMX_COMPONENT *comp, GOMX_PORT *port)
{
	OMX_ERRORTYPE r;
	OMX_BUFFERHEADERTYPE *hdr;
	void *buf;

	if (port->tunnel_supplier) {
		CINFO(comp, port, "Allocating tunnel buffers");
		while (port->num_buffers < port->def.nBufferCountActual) {
			pthread_mutex_unlock(&comp->mutex);
			r = OMX_ErrorInsufficientResources;
			buf = malloc(port->def.nBufferSize);
			if (buf) {
				r = OMX_UseBuffer(port->tunnel_comp, &hdr,
						port->tunnel_port, 0,
						port->def.nBufferSize, (OMX_U8*) buf);
				if (r != OMX_ErrorNone) free(buf);
			}
			if (r == OMX_ErrorInvalidState ||
			    r == OMX_ErrorIncorrectStateOperation) {
				/* Non-supplier is not transitioned yet.
				 * Wait for a bit and retry */
				usleep(1000);
				pthread_mutex_lock(&comp->mutex);
				continue;
			}
			pthread_mutex_lock(&comp->mutex);

			if (r != OMX_ErrorNone) {
				/* Hard error. Cancel and bail out */
				__gomx_port_unpopulate(comp, port);
				return r;
			}

			if (port->def.eDir == OMX_DirInput)
				hdr->nInputPortIndex = port->def.nPortIndex;
			else
				hdr->nOutputPortIndex = port->def.nPortIndex;
			gomxq_enqueue(&port->tunnel_supplierq, (void*) hdr);
			port->num_buffers++;
			__gomx_port_update_buffer_state(comp, port);
		}
	} else {
		/* Wait client / tunnel supplier to allocate buffers */
		CINFO(comp, port, "waiting buffers");
		while (!port->def.bPopulated)
			pthread_cond_wait(&port->cond_populated, &comp->mutex);
	}

	CINFO(comp, port, "POPULATED");
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE gomx_send_command(OMX_HANDLETYPE hComponent, OMX_COMMANDTYPE Cmd, OMX_U32 nParam1, OMX_PTR pCmdData)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	GOMX_COMMAND *c;

	/* OMX IL Specification is unclear which errors can be returned
	 * inline and which need to be reported with a callback.
	 * This just does minimal state checking, and queues everything
	 * to worker and reports any real errors via the callback. */
	if (!hComponent) return OMX_ErrorInvalidComponent;
	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;
	if (!comp->cb.EventHandler) return OMX_ErrorNotReady;

	c = (GOMX_COMMAND*) malloc(sizeof(GOMX_COMMAND));
	if (!c) return OMX_ErrorInsufficientResources;

	CINFO(comp, 0, "SendCommand %x, %x, %p", Cmd, nParam1, pCmdData);
	c->cmd = Cmd;
	c->param = nParam1;
	c->data = pCmdData;

	pthread_mutex_lock(&comp->mutex);
	gomxq_enqueue(&comp->cmdq, (void*) c);
	pthread_cond_signal(&comp->cond);
	pthread_mutex_unlock(&comp->mutex);

	return OMX_ErrorNone;
}

#define GOMX_TRANS(a,b) ((((uint32_t)a) << 16) | (uint32_t)b)

static OMX_ERRORTYPE gomx_do_set_state(GOMX_COMPONENT *comp, GOMX_COMMAND *cmd)
{
	OMX_STATETYPE new_state = (OMX_STATETYPE) cmd->param;
	OMX_ERRORTYPE r;
	GOMX_PORT *port;
	size_t i;

	if (comp->state == new_state) return OMX_ErrorSameState;

	if (new_state == OMX_StateInvalid) {
		/* Transition to invalid state is always valid and immediate */
		comp->state = new_state;
		return OMX_ErrorNone;
	}

	CDEBUG(comp, 0, "starting transition to state %d", new_state);

	comp->wanted_state = new_state;

	if (comp->statechange) {
		r = comp->statechange(comp);
		if (r != OMX_ErrorNone) goto err;
	}

	switch (GOMX_TRANS(comp->state, new_state)) {
	case GOMX_TRANS(OMX_StateLoaded, OMX_StateIdle):
		/* populate or wait for all enabled ports to be populated */
		for (i = 0; i < comp->nports; i++) {
			if (!comp->ports[i].def.bEnabled) continue;
			r = __gomx_port_populate(comp, &comp->ports[i]);
			if (r) goto err;
		}
		break;
	case GOMX_TRANS(OMX_StateIdle, OMX_StateLoaded):
		/* free or wait all ports to be unpopulated */
		for (i = 0; i < comp->nports; i++) {
			r = __gomx_port_unpopulate(comp, &comp->ports[i]);
			if (r) goto err;
		}
		break;
	case GOMX_TRANS(OMX_StateIdle, OMX_StateExecuting):
		/* start threads */
		r = OMX_ErrorInsufficientResources;
		if (comp->worker &&
		    pthread_create(&comp->worker_thread, 0, comp->worker, comp) != 0)
			goto err;
		break;
	case GOMX_TRANS(OMX_StateExecuting, OMX_StateIdle):
		/* stop/join threads & wait buffers to be returned to suppliers */
		if (comp->worker_thread) {
			pthread_mutex_unlock(&comp->mutex);
			pthread_join(comp->worker_thread, 0);
			pthread_mutex_lock(&comp->mutex);
			comp->worker_thread = 0;
		}
		for (i = 0; i < comp->nports; i++) {
			port = &comp->ports[i];
			if (!port->tunnel_supplier || !port->def.bEnabled) continue;
			while (port->tunnel_supplierq.num != port->num_buffers)
				pthread_cond_wait(&port->cond_idle, &comp->mutex);
		}
		break;
	default:
		/* FIXME: Pause and WaitForResources states not supported */
		r = OMX_ErrorIncorrectStateTransition;
		goto err;
	}
	comp->state = new_state;
	CDEBUG(comp, 0, "transition to state %d: success", new_state);
	return OMX_ErrorNone;
err:
	comp->wanted_state = comp->state;
	CDEBUG(comp, 0, "transition to state %d: result %x", new_state, r);
	return r;
}

static OMX_ERRORTYPE gomx_do_port_command(GOMX_COMPONENT *comp, GOMX_PORT *port, GOMX_COMMAND *cmd)
{
	OMX_ERRORTYPE r = OMX_ErrorNone;

	switch (cmd->cmd) {
	case OMX_CommandFlush:
		if (port->flush) r = port->flush(comp, port);
		break;
	case OMX_CommandPortEnable:
		port->def.bEnabled = OMX_TRUE;
		r = __gomx_port_populate(comp, port);
		if (r != OMX_ErrorNone)
			port->def.bEnabled = OMX_FALSE;
		break;
	case OMX_CommandPortDisable:
		port->def.bEnabled = OMX_FALSE;
		if (port->flush) port->flush(comp, port);
		r = __gomx_port_unpopulate(comp, port);
		break;
	default:
		r = OMX_ErrorNotImplemented;
		break;
	}
	return r;
}

static OMX_ERRORTYPE gomx_do_command(GOMX_COMPONENT *comp, GOMX_COMMAND *cmd)
{
	GOMX_PORT *port;

	switch (cmd->cmd) {
	case OMX_CommandStateSet:
		CINFO(comp, 0, "state %x", cmd->param);
		return gomx_do_set_state(comp, cmd);
	case OMX_CommandFlush:
	case OMX_CommandPortEnable:
	case OMX_CommandPortDisable:
		/* FIXME: OMX_ALL is not supported (but not used in omxplayer) */
		if (!(port = gomx_get_port(comp, cmd->param)))
			return OMX_ErrorBadPortIndex;
		CINFO(comp, port, "command %x", cmd->cmd);
		return gomx_do_port_command(comp, port, cmd);
	case OMX_CommandMarkBuffer:
		/* FIXME: Not implemented (but not used in omxplayer) */
	default:
		CINFO(comp, 0, "UNSUPPORTED %x, %x, %p", cmd->cmd, cmd->param, cmd->data);
		return OMX_ErrorNotImplemented;
	}
}

static void *gomx_worker(void *ptr)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) ptr;
	GOMX_PORT *port;
	GOMX_COMMAND *cmd;
	OMX_BUFFERHEADERTYPE *hdr;
	OMX_ERRORTYPE r;

	CINFO(comp, 0, "start");
	pthread_mutex_lock(&comp->mutex);
	while (comp->state != OMX_StateInvalid) {
		cmd = (GOMX_COMMAND *) gomxq_dequeue(&comp->cmdq);
		if (cmd) {
			r = gomx_do_command(comp, cmd);
			if (r == OMX_ErrorNone)
				__gomx_event(comp, OMX_EventCmdComplete,
					     cmd->cmd, cmd->param, cmd->data);
			else
				__gomx_event(comp, OMX_EventError, r, 0, 0);
		} else {
			pthread_cond_wait(&comp->cond, &comp->mutex);
		}

		if (comp->state != OMX_StateExecuting)
			continue;

		/* FIXME: Rate limit and retry if needed suppplier buffer enqueuing */
		for (size_t i = 0; i < comp->nports; i++) {
			port = &comp->ports[i];
			while ((hdr = (OMX_BUFFERHEADERTYPE*)gomxq_dequeue(&port->tunnel_supplierq)) != 0) {
				pthread_mutex_unlock(&comp->mutex);
				r = OMX_FillThisBuffer(port->tunnel_comp, hdr);
				pthread_mutex_lock(&comp->mutex);
				if (r != OMX_ErrorNone) {
					__gomx_port_queue_supplier_buffer(port, hdr);
					break;
				}
			}
		}
	}
	pthread_mutex_unlock(&comp->mutex);
	/* FIXME: make sure all buffers are returned and worker threads stopped */
	CINFO(comp, 0, "stop");
	return 0;
}

static OMX_ERRORTYPE gomx_set_callbacks(OMX_HANDLETYPE hComponent, OMX_CALLBACKTYPE* pCallbacks, OMX_PTR pAppData)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;
	if (comp->state != OMX_StateLoaded) return OMX_ErrorIncorrectStateOperation;
	pthread_mutex_lock(&comp->mutex);
	comp->omx.pApplicationPrivate = pAppData;
	comp->cb = *pCallbacks;
	pthread_mutex_unlock(&comp->mutex);
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE gomx_use_egl_image(OMX_HANDLETYPE hComponent,
		OMX_BUFFERHEADERTYPE** ppBufferHdr, OMX_U32 nPortIndex,
		OMX_PTR pAppPrivate, void* eglImage)
{
	return OMX_ErrorNotImplemented;
}

static OMX_ERRORTYPE gomx_component_role_enum(OMX_HANDLETYPE hComponent, OMX_U8 *cRole, OMX_U32 nIndex)
{
	return OMX_ErrorNotImplemented;
}

static void gomx_init(GOMX_COMPONENT *comp, const char *name, OMX_PTR pAppData, OMX_CALLBACKTYPE* pCallbacks, GOMX_PORT *ports, size_t nports)
{
	comp->omx.nSize = sizeof comp->omx;
	comp->omx.nVersion.nVersion = OMX_VERSION;
	comp->omx.pApplicationPrivate = pAppData;
	comp->omx.GetComponentVersion = gomx_get_component_version;
	comp->omx.SendCommand = gomx_send_command;
	comp->omx.GetParameter = gomx_get_parameter;
	comp->omx.GetState = gomx_get_state;
	comp->omx.ComponentTunnelRequest = gomx_component_tunnel_request;
	comp->omx.UseBuffer = gomx_use_buffer;
	comp->omx.AllocateBuffer = gomx_allocate_buffer;
	comp->omx.FreeBuffer = gomx_free_buffer;
	comp->omx.EmptyThisBuffer = gomx_empty_this_buffer;
	comp->omx.FillThisBuffer = gomx_fill_this_buffer;
	comp->omx.SetCallbacks = gomx_set_callbacks;
	comp->omx.UseEGLImage = gomx_use_egl_image;
	comp->omx.ComponentRoleEnum = gomx_component_role_enum;

	comp->name = name;
	comp->cb = *pCallbacks;
	comp->state = OMX_StateLoaded;
	comp->nports = nports;
	comp->ports = ports;

	gomxq_init(&comp->cmdq, offsetof(GOMX_COMMAND, next));
	pthread_cond_init(&comp->cond, 0);
	pthread_mutex_init(&comp->mutex, 0);
	pthread_create(&comp->component_thread, 0, gomx_worker, comp);

	for (size_t i = 0; i < comp->nports; i++) {
		GOMX_PORT *port = &comp->ports[i];
		pthread_cond_init(&port->cond_no_buffers, 0);
		pthread_cond_init(&port->cond_populated, 0);
		pthread_cond_init(&port->cond_idle, 0);
		gomxq_init(&port->tunnel_supplierq,
			port->def.eDir == OMX_DirInput
			? offsetof(OMX_BUFFERHEADERTYPE, pInputPortPrivate)
			: offsetof(OMX_BUFFERHEADERTYPE, pOutputPortPrivate));
	}
}

static void gomx_fini(GOMX_COMPONENT *comp)
{
	CINFO(comp, 0, "destroying");
	pthread_mutex_lock(&comp->mutex);
	comp->state = OMX_StateInvalid;
	pthread_cond_broadcast(&comp->cond);
	pthread_mutex_unlock(&comp->mutex);
	pthread_join(comp->component_thread, 0);

	for (size_t i = 0; i < comp->nports; i++) {
		GOMX_PORT *port = &comp->ports[i];
		pthread_cond_destroy(&port->cond_no_buffers);
		pthread_cond_destroy(&port->cond_populated);
		pthread_cond_destroy(&port->cond_idle);
	}
	pthread_mutex_destroy(&comp->mutex);
	pthread_cond_destroy(&comp->cond);
}

/* ALSA Sink OMX Component */

#define OMXALSA_PORT_AUDIO		0
#define OMXALSA_PORT_CLOCK		1

typedef struct _OMX_ALSASINK {
	GOMX_COMPONENT gcomp;
	GOMX_PORT port_data[2];
	GOMX_QUEUE playq;
	pthread_cond_t cond_play;
	size_t frame_size, sample_rate, play_queue_size;
	int64_t starttime;
	int32_t timescale;
	OMX_AUDIO_PARAM_PCMMODETYPE pcm;
	snd_pcm_format_t pcm_format;
	snd_pcm_state_t pcm_state;
	snd_pcm_sframes_t pcm_delay;
	char device_name[16];
} OMX_ALSASINK;

static OMX_ERRORTYPE omxalsasink_set_parameter(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nParamIndex, OMX_PTR pComponentParameterStructure)
{
	static const struct {
		snd_pcm_format_t fmt;
		unsigned char pcm_mode;
		unsigned char bits_per_sample;
		unsigned char numerical_data;
		unsigned char endianess;
	} fmtmap[] = {
		{ SND_PCM_FORMAT_S8,     OMX_AUDIO_PCMModeLinear,  8, OMX_NumericalDataSigned,   OMX_EndianLittle },
		{ SND_PCM_FORMAT_U8,     OMX_AUDIO_PCMModeLinear,  8, OMX_NumericalDataUnsigned, OMX_EndianLittle },
		{ SND_PCM_FORMAT_S16_LE, OMX_AUDIO_PCMModeLinear, 16, OMX_NumericalDataSigned,   OMX_EndianLittle },
		{ SND_PCM_FORMAT_U16_LE, OMX_AUDIO_PCMModeLinear, 16, OMX_NumericalDataUnsigned, OMX_EndianLittle },
		{ SND_PCM_FORMAT_S16_BE, OMX_AUDIO_PCMModeLinear, 16, OMX_NumericalDataSigned,   OMX_EndianBig },
		{ SND_PCM_FORMAT_U16_BE, OMX_AUDIO_PCMModeLinear, 16, OMX_NumericalDataUnsigned, OMX_EndianBig },
		{ SND_PCM_FORMAT_S24_LE, OMX_AUDIO_PCMModeLinear, 24, OMX_NumericalDataSigned,   OMX_EndianLittle },
		{ SND_PCM_FORMAT_U24_LE, OMX_AUDIO_PCMModeLinear, 24, OMX_NumericalDataUnsigned, OMX_EndianLittle },
		{ SND_PCM_FORMAT_S24_BE, OMX_AUDIO_PCMModeLinear, 24, OMX_NumericalDataSigned,   OMX_EndianBig },
		{ SND_PCM_FORMAT_U24_BE, OMX_AUDIO_PCMModeLinear, 24, OMX_NumericalDataUnsigned, OMX_EndianBig },
		{ SND_PCM_FORMAT_S32_LE, OMX_AUDIO_PCMModeLinear, 32, OMX_NumericalDataSigned,   OMX_EndianLittle },
		{ SND_PCM_FORMAT_U32_LE, OMX_AUDIO_PCMModeLinear, 32, OMX_NumericalDataUnsigned, OMX_EndianLittle },
		{ SND_PCM_FORMAT_S32_BE, OMX_AUDIO_PCMModeLinear, 32, OMX_NumericalDataSigned,   OMX_EndianBig },
		{ SND_PCM_FORMAT_U32_BE, OMX_AUDIO_PCMModeLinear, 32, OMX_NumericalDataUnsigned, OMX_EndianBig },
		{ SND_PCM_FORMAT_A_LAW,  OMX_AUDIO_PCMModeALaw,    8, OMX_NumericalDataUnsigned, OMX_EndianLittle },
		{ SND_PCM_FORMAT_MU_LAW, OMX_AUDIO_PCMModeMULaw,   8, OMX_NumericalDataUnsigned, OMX_EndianLittle },
	};
	OMX_ALSASINK *sink = (OMX_ALSASINK *) hComponent;
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	GOMX_PORT *port;
	OMX_AUDIO_PARAM_PCMMODETYPE *pmt;
	OMX_ERRORTYPE r;
	snd_pcm_format_t pcm_format = SND_PCM_FORMAT_UNKNOWN;

	/* Valid in OMX_StateLoaded or on disabled port... so can't check
	 * state without port number which is in index specific struct. */
	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;

	switch (nParamIndex) {
	case OMX_IndexParamAudioPcm:
		if ((r = omx_cast(pmt, pComponentParameterStructure))) return r;
		if (!(port = gomx_get_port(comp, pmt->nPortIndex))) return OMX_ErrorBadPortIndex;
		if (pmt->nPortIndex != OMXALSA_PORT_AUDIO) return OMX_ErrorBadParameter;

		if (comp->state != OMX_StateLoaded && port->def.bEnabled)
			return OMX_ErrorIncorrectStateOperation;

		for (size_t i = 0; i < ARRAY_SIZE(fmtmap); i++) {
			if (fmtmap[i].pcm_mode == pmt->ePCMMode &&
			    fmtmap[i].bits_per_sample == pmt->nBitPerSample &&
			    fmtmap[i].numerical_data == pmt->eNumData &&
			    fmtmap[i].endianess == pmt->eEndian) {
				pcm_format = fmtmap[i].fmt;
				break;
			}
		}
		if (pcm_format == SND_PCM_FORMAT_UNKNOWN)
			return OMX_ErrorBadParameter;

		memcpy(&sink->pcm, pmt, sizeof *pmt);
		sink->pcm_format = pcm_format;
		break;
	default:
		CINFO(comp, 0, "UNSUPPORTED %x, %p", nParamIndex, pComponentParameterStructure);
		return OMX_ErrorNotImplemented;
	}
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxalsasink_get_config(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nIndex, OMX_PTR pComponentConfigStructure)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	OMX_ALSASINK *sink = (OMX_ALSASINK *) hComponent;
	OMX_PARAM_U32TYPE *u32param;
	OMX_ERRORTYPE r;

	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;
	if (!sink->frame_size) return OMX_ErrorInvalidState;

	switch (nIndex) {
	case OMX_IndexConfigAudioRenderingLatency:
		if ((r = omx_cast(u32param, pComponentConfigStructure))) return r;
		/* Number of samples received but not played */
		pthread_mutex_lock(&comp->mutex);
		u32param->nU32 = sink->play_queue_size / sink->frame_size;
		if (sink->pcm_state == SND_PCM_STATE_RUNNING)
			u32param->nU32 += sink->pcm_delay;
		pthread_mutex_unlock(&comp->mutex);
		CDEBUG(comp, 0, "OMX_IndexConfigAudioRenderingLatency %d", u32param->nU32);
		break;
	default:
		CINFO(comp, 0, "UNSUPPORTED %x, %p", nIndex, pComponentConfigStructure);
		return OMX_ErrorNotImplemented;
	}
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxalsasink_set_config(OMX_HANDLETYPE hComponent, OMX_INDEXTYPE nIndex, OMX_PTR pComponentConfigStructure)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT*) hComponent;
	OMX_ALSASINK *sink = (OMX_ALSASINK*) hComponent;
	OMX_CONFIG_BOOLEANTYPE *bt;
	OMX_CONFIG_BRCMAUDIODESTINATIONTYPE *adest;
	OMX_ERRORTYPE r;

	if (comp->state == OMX_StateInvalid) return OMX_ErrorInvalidState;

	switch (nIndex) {
	case OMX_IndexConfigBrcmClockReferenceSource:
		if ((r = omx_cast(bt, pComponentConfigStructure))) return r;
		CDEBUG(comp, 0, "OMX_IndexConfigBrcmClockReferenceSource %d", bt->bEnabled);
		break;
	case OMX_IndexConfigBrcmAudioDestination:
		if ((r = omx_cast(adest, pComponentConfigStructure))) return r;
		strncpy(sink->device_name, (const char*) adest->sName, sizeof sink->device_name - 1);
		CDEBUG(comp, 0, "OMX_IndexConfigBrcmAudioDestination %s", adest->sName);
		break;
	default:
		CINFO(comp, 0, "UNSUPPORTED %x, %p", nIndex, pComponentConfigStructure);
		return OMX_ErrorNotImplemented;
	}
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxalsasink_get_extension_index(OMX_HANDLETYPE hComponent, OMX_STRING cParameterName, OMX_INDEXTYPE *pIndexType)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) hComponent;
	CINFO(comp, 0, "UNSUPPORTED '%s', %p", cParameterName, pIndexType);
	return OMX_ErrorNotImplemented;
}

static OMX_ERRORTYPE omxalsasink_deinit(OMX_HANDLETYPE hComponent)
{
	OMX_ALSASINK *sink = (OMX_ALSASINK *) hComponent;
	gomx_fini(&sink->gcomp);
	free(sink);
	return OMX_ErrorNone;
}

static void *omxalsasink_worker(void *ptr)
{
	GOMX_COMPONENT *comp = (GOMX_COMPONENT *) ptr;
	OMX_HANDLETYPE hComponent = (OMX_HANDLETYPE) comp;
	OMX_ALSASINK *sink = (OMX_ALSASINK *) hComponent;
	OMX_BUFFERHEADERTYPE *buf;
	GOMX_PORT *audio_port = &comp->ports[OMXALSA_PORT_AUDIO];
	GOMX_PORT *clock_port = &comp->ports[OMXALSA_PORT_CLOCK];
	snd_pcm_t *dev = 0;
	snd_pcm_sframes_t n, delay;
	snd_pcm_hw_params_t *hwp;
	snd_pcm_uframes_t buffer_size, period_size, period_size_max;
	SwrContext *resampler = 0;
	uint8_t *resample_buf = 0;
	int32_t timescale;
	uint64_t layout;
	size_t resample_bufsz;
	unsigned int in_sample_rate;
	unsigned int rate;
	struct timespec ts;
	int err;

	CINFO(comp, 0, "worker started");

	err = snd_pcm_open(&dev, sink->device_name, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) goto alsa_error;

	in_sample_rate = sink->pcm.nSamplingRate;
	rate = sink->pcm.nSamplingRate;
	buffer_size = rate / 5;
	period_size = buffer_size / 4;
	period_size_max = buffer_size / 3;

	snd_pcm_hw_params_alloca(&hwp);
	snd_pcm_hw_params_any(dev, hwp);
	err = snd_pcm_hw_params_set_channels(dev, hwp, sink->pcm.nChannels);
	if (err) goto alsa_error;
	err = snd_pcm_hw_params_set_access(dev, hwp, sink->pcm.bInterleaved ? SND_PCM_ACCESS_RW_INTERLEAVED : SND_PCM_ACCESS_RW_NONINTERLEAVED);
	if (err) goto alsa_error;
	err = snd_pcm_hw_params_set_rate_near(dev, hwp, &rate, 0);
	if (err) goto alsa_error;
	err = snd_pcm_hw_params_set_format(dev, hwp, sink->pcm_format);
	if (err) goto alsa_error;
	err = snd_pcm_hw_params_set_period_size_max(dev, hwp, &period_size_max, 0);
	if (err) goto alsa_error;
	err = snd_pcm_hw_params_set_buffer_size_near(dev, hwp, &buffer_size);
	if (err) goto alsa_error;
	err = snd_pcm_hw_params_set_period_size_near(dev, hwp, &period_size, 0);
	if (err) goto alsa_error;
	err = snd_pcm_hw_params(dev, hwp);
	if (err) goto alsa_error;

	sink->pcm.nSamplingRate = rate;
	sink->frame_size = (sink->pcm.nChannels * sink->pcm.nBitPerSample) >> 3;
	sink->sample_rate = rate;

	layout = av_get_default_channel_layout(sink->pcm.nChannels);
	resampler = swr_alloc_set_opts(NULL,
		/*out*/ layout, AV_SAMPLE_FMT_S16, rate,
		/*in*/ layout, AV_SAMPLE_FMT_S16, in_sample_rate,
		0, NULL);
	if (!resampler) goto err;

	av_opt_set_double(resampler, "cutoff", 0.985, 0);
	av_opt_set_int(resampler,"filter_size", 64, 0);
	if (swr_init(resampler) < 0) goto err;

	resample_bufsz = audio_port->def.nBufferSize * 2;
	resample_buf = (uint8_t *) malloc(resample_bufsz);
	if (!resample_buf) goto err;

	CINFO(comp, 0, "sample_rate %d, frame_size %d", rate, sink->frame_size);

	pthread_mutex_lock(&comp->mutex);
	while (comp->wanted_state == OMX_StateExecuting) {
		/* Update hw buffer length, and xrun state */
		sink->pcm_state = snd_pcm_state(dev);
		delay = 0;
		snd_pcm_delay(dev, &delay);
		if (resampler) delay += swr_get_delay(resampler, rate);
		sink->pcm_delay = delay;

		/* Wait for buffer, or timeout to refresh state */
		buf = 0;
		timescale = sink->timescale;
		if (timescale)
			buf = (OMX_BUFFERHEADERTYPE*) gomxq_dequeue(&sink->playq);
		if (!buf) {
			clock_gettime(CLOCK_MONOTONIC, &ts);
			ts.tv_nsec += 10000000UL; /* 10 ms */
			if (ts.tv_nsec >= 1000000000L) {
				ts.tv_nsec -= 1000000000UL;
				ts.tv_sec++;
			}
			pthread_cond_timedwait(&sink->cond_play, &comp->mutex, &ts);
			continue;
		}

		if (clock_port->tunnel_comp && !(buf->nFlags & OMX_BUFFERFLAG_TIME_UNKNOWN)) {
			OMX_TIME_CONFIG_TIMESTAMPTYPE tst;
			int64_t pts = omx_ticks_to_s64(buf->nTimeStamp);

			omx_init(tst);
			tst.nPortIndex = clock_port->tunnel_port;
			tst.nTimestamp = buf->nTimeStamp;
			if (resampler && buf->nFlags & OMX_BUFFERFLAG_STARTTIME)
				swr_init(resampler);
			if (buf->nFlags & (OMX_BUFFERFLAG_STARTTIME|OMX_BUFFERFLAG_DISCONTINUITY)) {
				CINFO(comp, 0, "STARTTIME nTimeStamp=%llx", pts);
				sink->starttime = pts;
			}

			pts -= (int64_t)sink->pcm_delay * OMX_TICKS_PER_SECOND / rate;

			pthread_mutex_unlock(&comp->mutex);
			if (buf->nFlags & (OMX_BUFFERFLAG_STARTTIME|OMX_BUFFERFLAG_DISCONTINUITY))
				OMX_SetConfig(clock_port->tunnel_comp, OMX_IndexConfigTimeClientStartTime, &tst);
			if (pts >= sink->starttime) {
				tst.nTimestamp = omx_ticks_from_s64(pts);
				OMX_SetConfig(clock_port->tunnel_comp, OMX_IndexConfigTimeCurrentAudioReference, &tst);
			}
			pthread_mutex_lock(&comp->mutex);
		}

		if (buf->nFlags & (OMX_BUFFERFLAG_DECODEONLY|OMX_BUFFERFLAG_CODECCONFIG|OMX_BUFFERFLAG_DATACORRUPT)) {
			CDEBUG(comp, 0, "skipping: %d bytes, flags %x", buf->nFilledLen, buf->nFlags);
			sink->play_queue_size -= buf->nFilledLen;
		} else {
			uint8_t *out_ptr, *in_ptr;
			int in_len, out_len;

			pthread_mutex_unlock(&comp->mutex);

			in_ptr = (uint8_t *)(buf->pBuffer + buf->nOffset);
			in_len = buf->nFilledLen / sink->frame_size;

			if (resampler) {
				int delta = 0;

				if (timescale != 0x10000 && timescale >= 0x0100 && timescale <= 0x20000)
					delta = ((int64_t)in_len*(0x10000-timescale))>>16;

				out_len = resample_bufsz / sink->frame_size;
				swr_set_compensation(resampler, delta, in_len);

				out_ptr = resample_buf;
				out_len = swr_convert(resampler, &out_ptr, out_len,
					(const uint8_t **) &in_ptr, in_len);

				if (out_len < 0) out_len = 0;
			} else {
				out_ptr = in_ptr;
				out_len = in_len;
			}

			pthread_mutex_lock(&comp->mutex);
			sink->play_queue_size -= buf->nFilledLen;
			sink->pcm_delay += out_len;
			pthread_mutex_unlock(&comp->mutex);

			while (out_len > 0) {
				n = snd_pcm_writei(dev, out_ptr, out_len);
				if (n < 0) {
					CINFO(comp, 0, "alsa error: %ld: %s", n, snd_strerror(n));
					snd_pcm_recover(dev, n, 1);
					n = 0;
				}
				out_len -= n;
				n *= sink->frame_size;
				out_ptr += n;
			}
			pthread_mutex_lock(&comp->mutex);
		}

		__gomx_process_mark(comp, buf);
		if (buf->nFlags & OMX_BUFFERFLAG_EOS) {
			CDEBUG(comp, 0, "end-of-stream");
			pthread_mutex_unlock(&comp->mutex);
			snd_pcm_drain(dev);
			snd_pcm_prepare(dev);
			pthread_mutex_lock(&comp->mutex);
			sink->pcm_state = SND_PCM_STATE_PREPARED;
			sink->pcm_delay = 0;
			__gomx_event(comp, OMX_EventBufferFlag, OMXALSA_PORT_AUDIO, buf->nFlags, 0);
		}
		__gomx_empty_buffer_done(comp, buf);
	}
	pthread_mutex_unlock(&comp->mutex);
cleanup:
	if (dev) snd_pcm_close(dev);
	if (resampler) swr_close(resampler);
	free(resample_buf);
	CINFO(comp, 0, "worker stopped");
	return 0;

alsa_error:
	CINFO(comp, 0, "ALSA error: %s", snd_strerror(err));
err:
	pthread_mutex_lock(&comp->mutex);
	/* FIXME: Current we just go to invalid state, but we might go
	 * back to Idle with ErrorResourcesPreempted and let the client
	 * recover. However, omxplayer does not care about this. */
	comp->state = OMX_StateInvalid;
	__gomx_event(comp, OMX_EventError, OMX_StateInvalid, 0, 0);
	pthread_mutex_unlock(&comp->mutex);
	goto cleanup;
}

static OMX_ERRORTYPE omxalsasink_audio_do_buffer(GOMX_COMPONENT *comp, GOMX_PORT *port, OMX_BUFFERHEADERTYPE *buf)
{
	OMX_ALSASINK *sink = (OMX_ALSASINK *) comp;
	sink->play_queue_size += buf->nFilledLen;
	gomxq_enqueue(&sink->playq, (void *) buf);
	pthread_cond_signal(&sink->cond_play);
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxalsasink_audio_flush(GOMX_COMPONENT *comp, GOMX_PORT *port)
{
	OMX_ALSASINK *sink = (OMX_ALSASINK *) comp;
	OMX_BUFFERHEADERTYPE *buf;
	while ((buf = (OMX_BUFFERHEADERTYPE *) gomxq_dequeue(&sink->playq)) != 0) {
		sink->play_queue_size -= buf->nFilledLen;
		__gomx_empty_buffer_done(comp, buf);
	}
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxalsasink_clock_do_buffer(GOMX_COMPONENT *comp, GOMX_PORT *port, OMX_BUFFERHEADERTYPE *buf)
{
	OMX_ALSASINK *sink = (OMX_ALSASINK *) comp;
	OMX_TIME_MEDIATIMETYPE *pMediaTime;
	int wake = 0;

	if (omx_cast(pMediaTime, buf->pBuffer) == OMX_ErrorNone) {
		CDEBUG(comp, port, "%p: clock %u bytes, flags=%x, nTimeStamp=%llx, eState=%d, xScale=%x",
			buf, buf->nFilledLen, buf->nFlags, omx_ticks_to_s64(buf->nTimeStamp),
			pMediaTime->eState, pMediaTime->xScale);
		wake = (!sink->timescale && pMediaTime->xScale);
		sink->timescale = pMediaTime->xScale;
	} else {
		CDEBUG(comp, port, "%p: clock %u bytes, flags=%x, nTimeStamp=%llx",
			buf, buf->nFilledLen, buf->nFlags, omx_ticks_to_s64(buf->nTimeStamp));
	}
	__gomx_process_mark(comp, buf);
	__gomx_empty_buffer_done(comp, buf);
	if (wake) pthread_cond_signal(&sink->cond_play);

	return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxalsasink_statechange(GOMX_COMPONENT *comp)
{
	OMX_ALSASINK *sink = (OMX_ALSASINK *) comp;
	pthread_cond_signal(&sink->cond_play);
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE omxalsasink_create(OMX_HANDLETYPE *pHandle, OMX_PTR pAppData, OMX_CALLBACKTYPE *pCallbacks)
{
	OMX_ALSASINK *sink;
	GOMX_PORT *port;
	pthread_condattr_t attr;

	sink = (OMX_ALSASINK *) calloc(1, sizeof *sink);
	if (!sink) return OMX_ErrorInsufficientResources;

	strncpy(sink->device_name, "default", sizeof sink->device_name - 1);
	gomxq_init(&sink->playq, offsetof(OMX_BUFFERHEADERTYPE, pInputPortPrivate));

	/* Audio port */
	port = &sink->port_data[OMXALSA_PORT_AUDIO];
	port->def.nSize = sizeof *port;
	port->def.nVersion.nVersion = OMX_VERSION;
	port->def.nPortIndex = OMXALSA_PORT_AUDIO;
	port->def.eDir = OMX_DirInput;
	port->def.nBufferCountMin = 4;
	port->def.nBufferCountActual = 4;
	port->def.nBufferSize = 8 * 1024;
	port->def.bEnabled = OMX_TRUE;
	port->def.eDomain = OMX_PortDomainAudio;
	port->def.format.audio.cMIMEType = (char *) "raw/audio";
	port->def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
	port->def.nBufferAlignment = 4;
	port->do_buffer = omxalsasink_audio_do_buffer;
	port->flush = omxalsasink_audio_flush;

	/* Clock port */
	port = &sink->port_data[OMXALSA_PORT_CLOCK];
	port->def.nSize = sizeof *port;
	port->def.nVersion.nVersion = OMX_VERSION;
	port->def.nPortIndex = OMXALSA_PORT_CLOCK;
	port->def.eDir = OMX_DirInput;
	port->def.nBufferCountMin = 1;
	port->def.nBufferCountActual = 1;
	port->def.nBufferSize = sizeof(OMX_TIME_MEDIATIMETYPE);
	port->def.bEnabled = OMX_TRUE;
	port->def.eDomain = OMX_PortDomainOther;
	port->def.format.other.eFormat = OMX_OTHER_FormatTime;
	port->def.nBufferAlignment = 4;
	port->do_buffer = omxalsasink_clock_do_buffer;

	gomx_init(&sink->gcomp, "OMX.alsa.audio_render", pAppData, pCallbacks, sink->port_data, ARRAY_SIZE(sink->port_data));
	sink->gcomp.omx.SetParameter = omxalsasink_set_parameter;
	sink->gcomp.omx.GetConfig = omxalsasink_get_config;
	sink->gcomp.omx.SetConfig = omxalsasink_set_config;
	sink->gcomp.omx.GetExtensionIndex = omxalsasink_get_extension_index;
	sink->gcomp.omx.ComponentDeInit = omxalsasink_deinit;
	sink->gcomp.worker = omxalsasink_worker;
	sink->gcomp.statechange = omxalsasink_statechange;

	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	pthread_cond_init(&sink->cond_play, &attr);
	pthread_condattr_destroy(&attr);

	*pHandle = (OMX_HANDLETYPE) sink;
	return OMX_ErrorNone;
}

/* OMX Glue to get the handle */

#include <OMXAlsa.h>

OMX_ERRORTYPE OMXALSA_GetHandle(OMX_OUT OMX_HANDLETYPE* pHandle, OMX_IN OMX_STRING cComponentName,
				OMX_IN  OMX_PTR pAppData, OMX_IN OMX_CALLBACKTYPE* pCallbacks)
{
	if (strcmp(cComponentName, "OMX.alsa.audio_render") == 0)
		return omxalsasink_create(pHandle, pAppData, pCallbacks);

	return OMX_ErrorComponentNotFound;
}

OMX_ERRORTYPE OMXALSA_FreeHandle(OMX_IN OMX_HANDLETYPE hComponent)
{
	return ((OMX_COMPONENTTYPE*)hComponent)->ComponentDeInit(hComponent);
}
