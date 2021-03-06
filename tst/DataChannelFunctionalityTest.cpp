#include "WebRTCClientTestFixture.h"

namespace com { namespace amazonaws { namespace kinesis { namespace video { namespace webrtcclient {

class DataChannelFunctionalityTest : public WebRtcClientTestBase {
};

// Macro so we don't have to deal with scope capture
#define TEST_DATA_CHANNEL_MESSAGE "This is my test message"

struct RemoteOpen {
    std::mutex lock{};
    std::map<std::string, uint64_t> channels{};
};

// Create two PeerConnections and ensure DataChannels that were declared
// before signaling go to connected
TEST_F(DataChannelFunctionalityTest, createDataChannel_Disconnected)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    RemoteOpen remoteOpen{};

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(createPeerConnection(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(createPeerConnection(&configuration, &answerPc), STATUS_SUCCESS);

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
      auto remoteOpen = reinterpret_cast<RemoteOpen*>(customData);
      DLOGD("onDataChannel '%s'", pRtcDataChannel->name);
      std::string name(pRtcDataChannel->name);
      {
          std::lock_guard<std::mutex> lock(remoteOpen->lock);
          if (remoteOpen->channels.count(name) == 0) {
              remoteOpen->channels.emplace(name, 1u);
          } else {
              auto count = remoteOpen->channels.at(name);
              remoteOpen->channels.erase(name);
              remoteOpen->channels.emplace(name, count + 1);
          }
      }
      dataChannelSend(pRtcDataChannel, FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
          ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peerConnectionOnDataChannel(offerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peerConnectionOnDataChannel(answerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(createDataChannel(offerPc, (PCHAR) "Offer PeerConnection", nullptr, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(createDataChannel(answerPc, (PCHAR) "Answer PeerConnection", nullptr, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(dataChannelOnOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(dataChannelOnOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(dataChannelOnMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(dataChannelOnMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until DataChannels connect and send a message
    for (auto i = 0; i <= 100 && (ATOMIC_LOAD(&datachannelLocalOpenCount) + ATOMIC_LOAD(&msgCount)) != 4 ; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    closePeerConnection(offerPc);
    closePeerConnection(answerPc);
    freePeerConnection(&offerPc);
    freePeerConnection(&answerPc);

    ASSERT_EQ(ATOMIC_LOAD(&datachannelLocalOpenCount), 2);
    ASSERT_EQ(ATOMIC_LOAD(&msgCount), 2);
    ASSERT_EQ(2, remoteOpen.channels.size());
    ASSERT_EQ(1, remoteOpen.channels.count("Offer PeerConnection"));
    ASSERT_EQ(1, remoteOpen.channels.count("Answer PeerConnection"));
    ASSERT_EQ(1u, remoteOpen.channels.at("Offer PeerConnection"));
    ASSERT_EQ(1u, remoteOpen.channels.at("Answer PeerConnection"));
}

TEST_F(DataChannelFunctionalityTest, dataChannelSendRecvMessageAfterDtlsCompleted)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T pOfferRemoteDataChannel = 0, pAnswerRemoteDataChannel = 0;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    BOOL dtlsCompleted = FALSE;

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
        ATOMIC_STORE((PSIZE_T) customData, (SIZE_T) pRtcDataChannel);
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
            ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(createPeerConnection(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(createPeerConnection(&configuration, &answerPc), STATUS_SUCCESS);

    EXPECT_EQ(peerConnectionOnDataChannel(offerPc, (UINT64) &pOfferRemoteDataChannel, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peerConnectionOnDataChannel(answerPc, (UINT64) &pAnswerRemoteDataChannel, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(createDataChannel(offerPc, (PCHAR) "Offer PeerConnection", nullptr, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(createDataChannel(answerPc, (PCHAR) "Answer PeerConnection", nullptr, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(dataChannelOnOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(dataChannelOnOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(dataChannelOnMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(dataChannelOnMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until remote channel open and dtls completed
    for (auto i = 0; i <= 100 && (dtlsSessionIsInitFinished(((PKvsPeerConnection) offerPc)->pDtlsSession, &dtlsCompleted) || ATOMIC_LOAD(&pOfferRemoteDataChannel) == 0); i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    EXPECT_EQ(dtlsCompleted, TRUE);
    EXPECT_TRUE(ATOMIC_LOAD(&pOfferRemoteDataChannel) != 0);

    EXPECT_EQ(dataChannelSend(
            (PRtcDataChannel) ATOMIC_LOAD(&pOfferRemoteDataChannel),
            FALSE,
            (PBYTE) TEST_DATA_CHANNEL_MESSAGE,
            STRLEN(TEST_DATA_CHANNEL_MESSAGE)), STATUS_SUCCESS);

    /* wait until the channel message is received */
    for (auto i = 0; i <= 5 && ATOMIC_LOAD(&msgCount) == 0; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }
    EXPECT_EQ(ATOMIC_LOAD(&msgCount), 1);

    closePeerConnection(offerPc);
    closePeerConnection(answerPc);
    freePeerConnection(&offerPc);
    freePeerConnection(&answerPc);
}

}
}
}
}
}
