#include <dpputils/audio/ytplayer.h>
#include <iostream>

namespace webm {

Status StreamCallback::OnClusterBegin(const ElementMetadata& metadata,
                              const Cluster& cluster, Action* action) 
{
    cluster_timecode = cluster.timecode.value();
    return Status(Status::kOkCompleted);
}

Status StreamCallback::OnTrackEntry(const ElementMetadata& metadata,
                                    const TrackEntry& track_entry)
{
    if (track_entry.track_type.value() == webm::TrackType::kAudio)
    {
        webm::Audio audio = track_entry.audio.value();
        if ((audio.sampling_frequency.value() == 48000) &&
            (track_entry.codec_id.value() == "A_OPUS"))
            return Status(Status::kOkCompleted);
        else
            return Status(Status::kInvalidElementValue);
    }
    else
        return Status(Status::kInvalidElementValue);
};

Status StreamCallback::OnSimpleBlockBegin(const ElementMetadata& metadata,
                                          const SimpleBlock& simple_block,
                                          Action* action)
{
    *action = Action::kRead;
    return Status(Status::kOkCompleted);
};

Status StreamCallback::OnBlockBegin(const ElementMetadata& metadata, 
                                    const Block& block, Action* action)
{
    *action = Action::kRead;
    return Status(Status::kOkCompleted);
};

Status StreamCallback::OnFrame(const FrameMetadata& metadata, 
                               Reader* reader, std::uint64_t* bytes_remaining)
{
    uint8_t *buf = (uint8_t *)malloc(*bytes_remaining + 1);
    if (!buf)
    {
        std::cerr << "Out of memory\n";
        exit(EXIT_FAILURE);
    }
    uint64_t read;
    
    Status status = reader->Read(*bytes_remaining, buf, &read);
    *bytes_remaining -= read;
    
    on_packet(this, (char *)buf, read);
    
    free(buf);
    return status;
};

}
