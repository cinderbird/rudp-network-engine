#pragma once
#include <cstdint>

struct AccoutInfo;

class INetworkService
{
public:
	virtual ~INetworkService() = default;
	virtual bool Initialize() = 0;
	virtual void Shutdown() = 0;
	virtual void Tick() = 0; //이거는 사용 안할 수도 있다

	//  TickSweep()        -- housekeeping + TickDispatch + TickFlush, 정확히 한 스레드만 호출 가능.
	//  MsUntilNextTick()  -- 그 스레드가 다음 스윕까지 잘 수 있는 시간.
	//  PumpIO(timeoutMs)  -- 소켓 완료 드레인. 스레드 수 제한 없음.
	//  WakeIO()           -- PumpIO에 대기 중인 스레드를 풀어 정지 요청을 신속히 감지하게 함.
	virtual void TickSweep() = 0;
	virtual unsigned int MsUntilNextTick() const = 0;
	// 마이크로초 정밀도 -- LaDelta::UsUntilNextTick 참고.
	virtual unsigned long long UsUntilNextTick() const = 0;
	virtual void PumpIO(unsigned int TimeoutMs) = 0;
	virtual void WakeIO() = 0;
	virtual unsigned int GetMaxTickRateHz() const = 0;
	virtual unsigned int StartOnlineGame(const AccoutInfo& Info) = 0;
	virtual unsigned int EndOnlineGame(const AccoutInfo& Info) = 0;
	virtual void Clear() = 0;

public:
	struct MessageHeader
	{
		uint16_t Size;
		uint16_t MessageId;
	};
};
