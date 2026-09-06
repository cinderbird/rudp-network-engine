#include "NetBunchBuilder.h"
#include "NetBunch.h"
#include "NetChannel.h"


ChannelBunchBuilder::ChannelBunchBuilder(uint8_t chIndex, bool bControl, bool bOpen, bool bClose, bool bReliable, BunchPolicy policy, NetChannel* Owner)
	: Policy(policy)
	, ChIndex(chIndex)
	, bControl(bControl)
	, bOpen(bOpen)
	, bClose(bClose)
	, bReliable(bReliable)
	, Owner(Owner)
{
	MaxBunchPayload = policy.MaxPacketBytes - policy.PacketHeaderBytes - policy.BunchHeaderBytes;
}

void ChannelBunchBuilder::Submit(const NetMessage& m)
{
	QueuedMessage.push(m);
}

void ChannelBunchBuilder::Pump(size_t batchLimit, bool flushTail)
{
	if (!ActiveHasInit) ResetActive();

	size_t taken = 0;
	while (taken < batchLimit && !QueuedMessage.empty())
	{
		auto QMessage = QueuedMessage.try_pop();
		if (QMessage)
		{

			++taken;
			if (!TryInsert(*QMessage))
			{
				SealAndPush(); // 꽉 참
				ResetActive();
				(void)TryInsert(*QMessage); // 첫 메시지 삽입은 반드시 성공
			}
			if (IsFull())
			{
				SealAndPush();
				ResetActive();
			}
		}
	}

	// 시간 임계치(최대 지연) 충족 시 강제 플러시
	if (Policy.MaxCoalesceUs > 0 && !Empty())
	{
		uint64_t now = NowUs();
		if (now - LastEmitUs >= Policy.MaxCoalesceUs)
		{
			SealAndPush();
			ResetActive();
		}
	}

	if (flushTail && !Empty())
	{
		SealAndPush();
		ResetActive();
	}
}

bool ChannelBunchBuilder::TryDequeueReady(std::shared_ptr<OutReadyBunch>& out)
{
	if (!QueuedOutBunch.empty())
	{
		out = QueuedOutBunch.front();
		QueuedOutBunch.pop();
		return true;
	}
	return false;
}

bool ChannelBunchBuilder::HasReadyBunch() const
{
	return !QueuedOutBunch.empty();
}


void ChannelBunchBuilder::DiscardOutReadyBunch(std::shared_ptr<OutReadyBunch>&& Bunch)
{
	for (auto& Message : Bunch->Messages)
	{
		if (Message.Callback)
		{
			Message.Callback(Message.BufferPtr, Message.Size, Message.Context);
		}
	}
}

bool ChannelBunchBuilder::IsHeaderOnlyBunch(const std::shared_ptr<OutReadyBunch>& Bunch)
{
	return (Bunch->TotalSize <= Bunch->HeaderSize);
}

void ChannelBunchBuilder::ResetActive()
{
	ActiveMsgs.clear();
	ActiveMsgs.reserve(128);
	ActivePayload = 0;
	ActiveHasInit = true;
	LastEmitUs = NowUs();
}

bool ChannelBunchBuilder::Empty() const
{
	return ActiveMsgs.empty();
}

bool ChannelBunchBuilder::IsFull()  const
{
	return ActiveMsgs.size() >= Policy.MaxMsgsPerBunch || ActivePayload >= MaxBunchPayload;
}

bool ChannelBunchBuilder::TryInsert(const NetMessage& m)
{
	if (ActiveMsgs.size() >= Policy.MaxMsgsPerBunch)
	{
		return false;
	}

	if (ActivePayload + m.Size > MaxBunchPayload)
	{
		return false;
	}

	ActiveMsgs.push_back(m);
	ActivePayload += m.Size;
	return true;
}

void ChannelBunchBuilder::SealAndPush()
{
	// 1) 헤더 인코딩
	BunchHeaderFields HeaderField{};
	HeaderField.bControl = bControl;
	HeaderField.bOpen = bOpen;
	HeaderField.bClose = bClose;
	HeaderField.bReliable = bReliable;
	HeaderField.ChIndex = ChIndex;
	HeaderField.ChSequence = ++Owner->OutReliable;
	HeaderField.MessageCount = static_cast<uint8_t>(ActiveMsgs.size());
	HeaderField.PayloadSize = (uint16_t)ActivePayload;

	BunchHeader Header{ 128 };
	Header.Encode(HeaderField);

	// 2) OutReadyBunch 생성

	std::shared_ptr<OutReadyBunch> Bunch = CORE::TMakeShared<OutReadyBunch>();
	Bunch->HeaderSize = Header.Size();
	Bunch->HeaderBytes.reset(new uint8_t[Bunch->HeaderSize]);

	//헤더 복사
	std::memcpy(Bunch->HeaderBytes.get(), Header.Data(), Bunch->HeaderSize);

	Bunch->Messages = std::move(ActiveMsgs); // zero-copy move

	Bunch->PayloadBytes = ActivePayload;
	Bunch->TotalSize = ActivePayload + Bunch->HeaderSize;

	Bunch->ChIndex = ChIndex;

	//spdlog::debug("BunchBuilder AddBunch, MessageCount: {}", Bunch->Messages.size());

	// 3) 큐에 push
	QueuedOutBunch.push(Bunch);
}

uint64_t ChannelBunchBuilder::NowUs()
{
	return Network::Clock::NowUs();
}
