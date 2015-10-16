#include "OMXDanuReader.h"
#include "OMXClock.h"

#include <regex>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/time.h>
#include <unistd.h>

#include <libashe/Number.h>

#include <chrono>
#include <string>
#include <iostream>
#include <vector>

#define __ISOPENED (this->__danuDemuxer || this->__danuInput)

namespace danu
{

OMXDanuReader::OMXDanuReader() noexcept
{
}

OMXDanuReader::~OMXDanuReader() noexcept
{
	this->__close();
}

bool OMXDanuReader::Open(std::string filename, bool dump_format, bool live, float timeout, std::string cookie, std::string user_agent)
{
	const std::chrono::system_clock::time_point __funcStart = std::chrono::system_clock::now();
	std::chrono::system_clock::time_point __tp;
	const static std::regex __EXP__("danu:\\/\\/.*:[0-9]+");
	std::regex_iterator<std::string::iterator> it(filename.begin(), filename.end(), __EXP__), itEnd;
	if(it == itEnd)
		return motherClass::Open(filename, dump_format, live, timeout, cookie, user_agent);
	else if(__ISOPENED)
		this->__close();

	auto __throw__ = [](std::string x)
	{
		x += '\n';
		::perror(x.c_str());
		throw x;
	};

	try
	{
		struct timeval to;
		sockaddr_in addr;
		Danu_MediaStreamContext ctx;
		to.tv_sec = (__time_t)timeout;
		to.tv_usec = (__suseconds_t)((timeout - (float)((uint32_t)timeout)) * 1000000.0f);
		__name = __port = it->str();

		__name = std::regex_replace(__name, std::regex("danu:\\/\\/"), "");
		__name = std::regex_replace(__name, std::regex(":[0-9]+"), "");
		__port = std::regex_replace(__port, std::regex("danu:\\/\\/.*:"), "");

		this->__danuFD = ::socket(AF_INET, SOCK_STREAM, 0);
		if(this->__danuFD < 0)
			return false;
		::setsockopt(this->__danuFD, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(timeout));
		::setsockopt(this->__danuFD, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(timeout));

		::memset(&addr, 0, sizeof(sockaddr_in));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr(__name.c_str());
		addr.sin_port = htons(ashe::Number<uint16_t>::fromString(__port));
		if(::connect(this->__danuFD, (sockaddr*)&addr, sizeof(addr)) < 0)
			__throw__("Connection failed.");

		if(!(this->__danuDemuxer = ::danu_openMediaDemuxer()))
			__throw__("Failed to init Denu MediaDemuxer.");
		else if(!(this->__danuInput = ::danu_openMediaDemuxerInput(this->__danuDemuxer, DANU_MDT_NONE)))
			__throw__("Failed to init Denu MediaDemuxer **Input**.");

		danu_setMediaDemuxer_maxPendingContexts(this->__danuDemuxer, 1);

		while(! this->m_open)
		{
			this->__danuRead();
			__tp = std::chrono::system_clock::now();
			if((float)std::chrono::duration_cast<std::chrono::seconds>(__tp - __funcStart).count() > timeout)
				__throw__("Timed-out whilst determining context.");
		}
		if(!(this->m_video_count > 0 || this->m_audio_count > 0))
			__throw__("No media context.");
	}
	catch(std::regex_error &e)
	{
		std::cerr << __FILE__ << ':' << __LINE__ << " exception caught: " << e.what() << std::endl;
		this->__close();
		return false;
	}
	catch(...)
	{
		std::cerr << __FILE__ << ':' << __LINE__ << " exception caught." << std::endl;
		this->__close();
		return false;
	}

	this->m_seek = false;
	this->m_bAVI = false;
	this->m_bMatroska = false;
	this->m_speed = DVD_PLAYSPEED_NORMAL;
	this->m_iCurrentPts = DVD_NOPTS_VALUE;
	// TODO: Spawn the poll thread
	this->__pollThread = std::thread([this](){
		this->__pollThreadRun();
	});

	return true;
}

bool OMXDanuReader::Close()
{
	this->__close();
	return motherClass::Close();
}

bool OMXDanuReader::SeekTime(int time, bool backwords, double* startpts)
{
	if(__ISOPENED)
		return false;
	return false;
}

AVMediaType OMXDanuReader::PacketType(OMXPacket* pkt)
{
	if(!pkt)
		return AVMEDIA_TYPE_UNKNOWN;
	if(__ISOPENED)
		return AVMEDIA_TYPE_VIDEO;
	return motherClass::PacketType(pkt);
}

OMXPacket* OMXDanuReader::Read()
{
	if(!__ISOPENED)
		return motherClass::Read();
	OMXPacket *ret = NULL;
	this->__danuRead(&ret);
	return ret;
}

bool OMXDanuReader::IsActive(OMXStreamType type, int stream_index)
{
	if(__ISOPENED)
		return true;
	return motherClass::IsActive(type, stream_index);
}

bool OMXDanuReader::SetActiveStream(OMXStreamType type, unsigned int index)
{
	if(__ISOPENED)
		return true;
	return motherClass::SetActiveStream(type, index);
}

void OMXDanuReader::SetSpeed(int iSpeed)
{
	if(__ISOPENED)
		return;
	motherClass::SetSpeed(iSpeed);
}

int OMXDanuReader::GetChapter()
{
	if(__ISOPENED)
		return 0;
	return motherClass::GetChapter();
}

bool OMXDanuReader::SeekChapter(int chapter, double* startpts)
{
	if(__ISOPENED)
		return false;
	return motherClass::SeekChapter(chapter, startpts);
}

int OMXDanuReader::__danuRead(OMXPacket **pkt/* = NULL*/) noexcept
{
	Danu_MediaPayloadHead head;
	head.payloadSize = 0;
	int32_t feedRet;
	int retSize = 0;
	ssize_t rwSize;
	auto __getPayload = [this, &head, &pkt, &rwSize]() -> bool
	{
		double theTS;
		timeval theTV;
		danu_peekMediaDemuxer_payload(this->__danuDemuxer, this->__danuContext.contextID, &head, NULL, 0);
		if(head.payloadSize == 0 || pkt == NULL)
		{
			// TODO: Dispose
			danu_getMediaDemuxer_payload(this->__danuDemuxer, this->__danuContext.contextID, NULL, NULL, 0);
			return false;
		}
		OMXPacket &out = *(*pkt = motherClass::AllocPacket((int)head.payloadSize));
		danu_getMediaDemuxer_payload(this->__danuDemuxer, this->__danuContext.contextID, NULL, out.data, out.size);
		if(::gettimeofday(&theTV, NULL) >= 0)
		{
			theTS = (double)theTV.tv_usec + ((double)theTV.tv_sec * 1000000.0);
			if(this->__baseTS < 0.0)
				this->__baseTS = theTS;
			out.dts = out.pts = theTS - this->__baseTS;
		}

		switch(head.type)
		{
		case DANU_MPT_AUDIOFRAME:
			out.codec_type = AVMEDIA_TYPE_AUDIO;
			break;
		case DANU_MPT_PFRAME:
		case DANU_MPT_IFRAME:
			out.codec_type = AVMEDIA_TYPE_VIDEO;
			break;
		default:
			out.codec_type = AVMEDIA_TYPE_UNKNOWN;
		}
		out.hints = this->m_streams[0].hints;
		out.stream_index = 0;
		return true;
	};

	if(this->__danuFD < 0 || ! __ISOPENED)
		return -1;
	else if(this->m_open && __getPayload())
		return 0;

	while(true)
	{
		retSize += rwSize = ::read(this->__danuFD, this->__danuBuf.data(), this->__danuBuf.size());
		if(rwSize <= 0)
		{
			this->Close();
			break;
		}

		feedRet = danu_feedMediaDemuxerInput(this->__danuInput, this->__danuBuf.data(), rwSize);
		if(feedRet & DANU_MEDIAMEMUXER_PENDING_CONTEXT)
		{
			OMXStream *stream;
			this->__hasContext = true;
			if(danu_getMediaDemuxer_contextIDs(this->__danuDemuxer, DANU_MEDIAMEMUXER_PENDING_CONTEXT, &this->__danuContext.contextID, 1) >= 0)
			{
				danu_getMediaDemuxer_context(this->__danuDemuxer, this->__danuContext.contextID, &this->__danuContext);
				if(this->__danuContext.speakerLayout != DANU_SSL_NONE)
				{
					stream = &this->m_streams[1];
					this->m_audio_count = 1;
					this->m_audio_index = 1;
					stream->index = 1;
					stream->id = 1;
					stream->type = OMXSTREAM_AUDIO;
				}
				else if(this->__danuContext.width > 0 && this->__danuContext.height > 0)
				{
					stream = &this->m_streams[0];
					this->m_video_count = 1;
					this->m_video_index = 0;
					stream->index = 0;
					stream->id = 0;
					stream->type = OMXSTREAM_VIDEO;
				}
				else
					return -1;
			}
			else
				return -1;

			this->m_open = true;
			this->m_eof = false;
			stream->stream = NULL;
			switch(this->__danuContext.encoding)
			{
			case DANU_ME_H264:
				stream->codec_name = "h264";
				stream->hints.codec = AV_CODEC_ID_H264;
				break;
			case DANU_ME_MJPEG:
				stream->codec_name = "mjpeg";
				stream->hints.codec = AV_CODEC_ID_MJPEG;
				break;
			case DANU_ME_MPEG4VIDEO:
				stream->codec_name = "mpeg4";
				stream->hints.codec = AV_CODEC_ID_MPEG4;
				break;
			case DANU_ME_MXPEG:
				stream->codec_name = "mxpeg";
				stream->hints.codec = AV_CODEC_ID_MXPEG;
				break;
			}
			stream->name = this->__name + ":" + this->__port;
			stream->hints.width = this->__danuContext.width;
			stream->hints.height = this->__danuContext.height;

			break;
		}
		if(this->m_open && (feedRet & DANU_MEDIAMEMUXER_PENDING_PAYLOAD))
		{
			if(__getPayload())
				break;
			return -1;
		}
	}

	return retSize;
}

void OMXDanuReader::__close() noexcept
{
	if(this->__danuDemuxer)
	{
		danu_closeMediaDemuxer_safely(&this->__danuDemuxer);
		this->__danuInput = NULL;
	}
	if(this->__danuFD >= 0)
	{
		::close(this->__danuFD);
		this->__danuFD = -1;
	}
	this->m_open = false;
	if(this->__pollThread.joinable())
	{
		{
			std::unique_lock<std::mutex> ul(this->__pollThreadCVMtx);
			this->__pollThreadCV.notify_all();
		}
		this->__pollThread.join();
	}
	this->__hasContext = false;
	this->__baseTS = -1.0;
}

void OMXDanuReader::__pollThreadRun() noexcept
{
	Danu_MediaProtocolProcessorType myProc, srcProc;
	uint16_t srcID, myID;
	std::vector<uint8_t> pollMsg(DANU_MEDIADEMUXER_POLLMESSAGE_LENGTH__);

	myProc = DANU_MPPT_DECODER_VIEW;
	myID = 0;

	while(this->m_open)
	{
		if(this->__hasContext)
		{
			srcProc = this->__danuContext.sourceProcessor;
			srcID = this->__danuContext.sourceProcessorID;
			danu_mediaDemuxer_pollMessage__(myProc, myID, srcProc, srcID, pollMsg.data());
			::write(this->__danuFD, pollMsg.data(), pollMsg.size()) != pollMsg.size();
		}

		{
			std::unique_lock<std::mutex> ul(this->__pollThreadCVMtx);
			this->__pollThreadCV.wait_for(ul, std::chrono::seconds(5));
		}
	}
}

} /* namespace danu */
