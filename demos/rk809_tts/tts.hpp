#pragma once
#include <memory>
#include <string>
#include <vector>
namespace speech {
// Pimpl 把 RKNN 资源封装在实现文件中；禁止复制，防止重复释放。
class MeloTts {
public:
    explicit MeloTts(const std::string& model_dir);
    ~MeloTts();
    MeloTts(const MeloTts&) = delete;
    MeloTts& operator=(const MeloTts&) = delete;
    std::vector<float> synthesize(const std::string& text);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
