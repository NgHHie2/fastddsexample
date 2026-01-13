#pragma once
#include <memory>
#include <string>
#include <mutex>

struct CoordinateData {
    double longitude;
    double latitude;
    int64_t timestamp;
    uint32_t sequence;
    
    CoordinateData() 
        : longitude(0.0)
        , latitude(0.0)
        , timestamp(0)
        , sequence(0)
    {}
    
    CoordinateData(double lon, double lat, int64_t ts, uint32_t seq)
        : longitude(lon)
        , latitude(lat)
        , timestamp(ts)
        , sequence(seq)
    {}
    
    std::string to_json() const {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), 
                 "{\"coords\":[%.8f,%.8f],\"time\":%lld,\"seq\":%u}",
                 longitude, latitude, (long long)timestamp, sequence);
        return std::string(buffer);
    }
    
    std::string to_csv() const {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), 
                 "%.8f,%.8f,%lld",
                 longitude, latitude, (long long)timestamp);
        return std::string(buffer);
    }
};

// C++11 compatible version using mutex
class SharedCoordinateState {
private:
    mutable std::mutex mutex_;
    std::shared_ptr<const CoordinateData> latest_;
    
public:
    SharedCoordinateState() {
        latest_ = std::make_shared<CoordinateData>();
    }
    
    // Producer: update với tọa độ mới
    void update(double lon, double lat, int64_t timestamp, uint32_t sequence) {
        auto new_data = std::make_shared<CoordinateData>(lon, lat, timestamp, sequence);
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = new_data;
    }
    
    // Consumer: đọc tọa độ mới nhất (thread-safe)
    std::shared_ptr<const CoordinateData> get_latest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_;
    }
    
    // Kiểm tra xem có data chưa
    bool has_data() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_ && latest_->sequence > 0;
    }
};