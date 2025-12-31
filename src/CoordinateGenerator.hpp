#pragma once
#include <cmath>
#include <chrono>

class CoordinateGenerator {
private:
    struct Point {
        double x;
        double y;
    };
    
    Point center_;
    double amplitude_;
    double frequency_;
    double t_;
    
public:
    CoordinateGenerator(double center_lon = 107.02243, 
                       double center_lat = 20.76300,
                       double amplitude = 0.05,
                       double frequency = 0.01)
        : center_{center_lon, center_lat}
        , amplitude_(amplitude)
        , frequency_(frequency)
        , t_(0.0)
    {
    }
    
    // Tính tọa độ theo hình số 8 (figure eight/lemniscate)
    std::pair<double, double> get_next_coordinate() {
        double x = amplitude_ * std::sin(t_);
        double y = amplitude_ * std::sin(t_) * std::cos(t_);
        
        double lon = center_.x + x;
        double lat = center_.y + y;
        
        t_ += frequency_;
        
        return {lon, lat};
    }
    
    // Reset về điểm bắt đầu
    void reset() {
        t_ = 0.0;
    }
    
    // Lấy timestamp hiện tại
    static int64_t get_timestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
};