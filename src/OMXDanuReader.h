#ifndef DANU_OMXDANUREADER_H_
#define DANU_OMXDANUREADER_H_

#include "OMXReader.h"

#include <libdanu/MediaDemuxer.h>

#include <list>
#include <set>
#include <array>

namespace danu
{

class OMXDanuReader: public OMXReader
{
public:
	typedef OMXReader motherClass;
	typedef OMXDanuReader thisClass;

protected:
	Danu_MediaDemuxer_t __danuDemuxer = NULL;
	Danu_MediaDemuxerInput_t __danuInput = NULL;
	double __baseTS = -1.0;
	int __danuFD = -1;

	Danu_MediaStreamContext __danuContext;
	std::array<uint8_t, 4096> __danuBuf;
	std::string __port, __name;

	int __danuRead(OMXPacket **pkt = NULL) noexcept;

private:
	void ____close() noexcept;

public:
	OMXDanuReader() noexcept;
	virtual ~OMXDanuReader() noexcept;

	virtual bool Open(std::string filename, bool dump_format, bool live = false, float timeout = 0.0f, std::string cookie = "", std::string user_agent = "");
	virtual bool Close();
	virtual bool SeekTime(int time, bool backwords, double *startpts);
	virtual AVMediaType PacketType(OMXPacket *pkt);
	virtual OMXPacket *Read();

	virtual bool IsActive(OMXStreamType type, int stream_index);

	virtual bool SetActiveStream(OMXStreamType type, unsigned int index);

	virtual void SetSpeed(int iSpeed);

	virtual int GetChapter();
	virtual bool SeekChapter(int chapter, double* startpts);
};

} /* namespace danu */

#endif /* SRC_OMXDANUREADER_H_ */
