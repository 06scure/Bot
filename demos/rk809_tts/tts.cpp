#include "tts.hpp"
#include "rknn_api.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace speech {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
void check(int result, const char* operation) {
    if (result != RKNN_SUCC) throw std::runtime_error(std::string(operation)+" error="+std::to_string(result));
}
struct Tensor { const char* name; unsigned elements; rknn_tensor_type type; };
class Model {
    rknn_context ctx_{};
    std::vector<unsigned> output_sizes_;
public:
    Model(const std::string& path, const std::vector<Tensor>& inputs,
          const std::vector<Tensor>& outputs) {
        try {
            check(rknn_init(&ctx_,const_cast<char*>(path.c_str()),0,0,nullptr),"rknn_init");
            rknn_input_output_num count{};
            check(rknn_query(ctx_,RKNN_QUERY_IN_OUT_NUM,&count,sizeof(count)),"query counts");
            if (count.n_input != inputs.size() || count.n_output != outputs.size())
                throw std::runtime_error("wrong model input/output count: "+path);
            for (int direction=0;direction<2;++direction) {
                const auto& specs = direction ? outputs : inputs;
                for (unsigned i=0;i<specs.size();++i) {
                    rknn_tensor_attr attr{}; attr.index=i;
                    check(rknn_query(ctx_,direction ? RKNN_QUERY_OUTPUT_ATTR : RKNN_QUERY_INPUT_ATTR,
                                     &attr,sizeof(attr)),"query tensor");
                    if (attr.n_elems!=specs[i].elements || std::string(attr.name)!=specs[i].name ||
                        (specs[i].type==RKNN_TENSOR_INT64 && attr.type!=RKNN_TENSOR_INT64))
                        throw std::runtime_error("incompatible tensor: "+std::string(attr.name));
                    if (direction) output_sizes_.push_back(attr.n_elems);
                }
            }
        } catch (...) { if(ctx_) rknn_destroy(ctx_); ctx_=0; throw; }
    }
    ~Model() { if(ctx_) rknn_destroy(ctx_); }
    Model(const Model&)=delete;
    Model& operator=(const Model&)=delete;
    std::vector<std::vector<float>> infer(std::vector<rknn_input> inputs) {
        for (unsigned i=0;i<inputs.size();++i) inputs[i].index=i;
        check(rknn_inputs_set(ctx_,inputs.size(),inputs.data()),"inputs_set");
        check(rknn_run(ctx_,nullptr),"rknn_run");
        std::vector<rknn_output> outputs(output_sizes_.size());
        for (unsigned i=0;i<outputs.size();++i) { outputs[i].index=i; outputs[i].want_float=1; }
        check(rknn_outputs_get(ctx_,outputs.size(),outputs.data(),nullptr),"outputs_get");
        std::vector<std::vector<float>> result;
        try {
            for (unsigned i=0;i<outputs.size();++i) {
                if (!outputs[i].buf || outputs[i].size < output_sizes_[i]*sizeof(float))
                    throw std::runtime_error("short RKNN output");
                const auto* begin=static_cast<float*>(outputs[i].buf);
                result.emplace_back(begin,begin+output_sizes_[i]);
            }
        } catch (...) { rknn_outputs_release(ctx_,outputs.size(),outputs.data()); throw; }
        check(rknn_outputs_release(ctx_,outputs.size(),outputs.data()),"outputs_release");
        return result;
    }
};
template<typename T> rknn_input input(std::vector<T>& data, rknn_tensor_type type) {
    rknn_input in{}; in.type=type; in.fmt=RKNN_TENSOR_UNDEFINED;
    in.buf=data.data(); in.size=data.size()*sizeof(T); return in;
}
using Phones = std::pair<std::vector<std::int64_t>,std::vector<std::int64_t>>;
}
struct MeloTts::Impl {
    std::unordered_map<std::string,Phones> lexicon;
    Model encoder;
    Model decoder;
    explicit Impl(const std::string& dir)
        : encoder(dir+"/encoder.rknn",
            {{"x",256,RKNN_TENSOR_INT64},{"x_lengths",1,RKNN_TENSOR_INT64},
             {"sid",1,RKNN_TENSOR_INT64},{"tone",256,RKNN_TENSOR_INT64},
             {"lang_ids",256,RKNN_TENSOR_INT64},{"ja_bert",768*256,RKNN_TENSOR_FLOAT32},
             {"noise_scale_w",1,RKNN_TENSOR_FLOAT32},{"sdp_ratio",1,RKNN_TENSOR_FLOAT32}},
            {{"logw",256,RKNN_TENSOR_FLOAT32},{"x_mask",256,RKNN_TENSOR_FLOAT32},
             {"g",256,RKNN_TENSOR_FLOAT32},{"m_p",192*256,RKNN_TENSOR_FLOAT32},
             {"logs_p",192*256,RKNN_TENSOR_FLOAT32}}),
          decoder(dir+"/decoder.rknn",
            {{"attn",512*256,RKNN_TENSOR_FLOAT32},{"y_mask",512,RKNN_TENSOR_FLOAT32},
             {"g",256,RKNN_TENSOR_FLOAT32},{"m_p",192*256,RKNN_TENSOR_FLOAT32},
             {"logs_p",192*256,RKNN_TENSOR_FLOAT32},{"noise_scale",1,RKNN_TENSOR_FLOAT32}},
            {{"y",512*512,RKNN_TENSOR_FLOAT32}}) {
        const auto start=Clock::now();
        std::ifstream tokens_file(dir+"/tokens.txt"), lexicon_file(dir+"/lexicon.txt");
        if (!tokens_file || !lexicon_file) throw std::runtime_error("missing lexicon/tokens");
        std::unordered_map<std::string,int> tokens;
        std::string token; int id;
        while(tokens_file>>token>>id) tokens.emplace(token,id);
        std::string line;
        while(std::getline(lexicon_file,line)) {
            std::istringstream stream(line); std::string word,part; stream>>word;
            std::vector<std::string> parts;
            while(stream>>part) parts.push_back(part);
            if (word.empty() || parts.empty() || parts.size()%2) throw std::runtime_error("bad lexicon row");
            Phones values;
            for (std::size_t i=0;i<parts.size()/2;++i) {
                values.first.push_back(tokens.at(parts[i]));
                values.second.push_back(std::stoi(parts[i+parts.size()/2]));
            }
            lexicon.emplace(word,std::move(values));
        }
        for (const std::string p : {"_"," ","!","?","…",",",".","'","-"})
            lexicon[p]={{tokens.at(p==" " ? "_" : p)},{0}};
        lexicon["嗯"]=lexicon.at("恩"); lexicon["呣"]=lexicon.at("母");
        std::cout<<"lexicon_ms="<<ms(start)<<'\n';
    }
    Phones frontend(const std::string& text) {
        if(text.empty()) throw std::invalid_argument("empty text");
        Phones result{{0},{0}};
        const auto padded="_"+text+"_";
        for(std::size_t pos=0;pos<padded.size();) {
            const unsigned char c=padded[pos];
            std::size_t len= c<128 ? 1 : (c&0xe0)==0xc0 ? 2 : (c&0xf0)==0xe0 ? 3 : (c&0xf8)==0xf0 ? 4 : 0;
            if (!len || pos+len>padded.size()) throw std::runtime_error("invalid UTF-8 text");
            const auto english=[](unsigned char v){return (v>='a'&&v<='z')||(v>='A'&&v<='Z');};
            if(english(c)) while(pos+len<padded.size()&&english(padded[pos+len])) ++len;
            auto word=padded.substr(pos,len); pos+=len;
            for(char& v:word) if(v>='A'&&v<='Z') v+=32;
            if(word=="，") word=",";
            if(word=="。") word=".";
            if(word=="！") word="!";
            if(word=="？") word="?";
            const auto found=lexicon.find(word);
            if(found==lexicon.end()) throw std::runtime_error("lexicon missing token: "+word);
            for(std::size_t i=0;i<found->second.first.size();++i) {
                result.first.push_back(found->second.first[i]); result.first.push_back(0);
                result.second.push_back(found->second.second[i]); result.second.push_back(0);
            }
        }
        if(result.first.size()>256) throw std::runtime_error("text exceeds 256 phones; split sentence");
        return result;
    }
};
MeloTts::MeloTts(const std::string& dir):impl_(std::make_unique<Impl>(dir)) {}
MeloTts::~MeloTts()=default;
std::vector<float> MeloTts::synthesize(const std::string& text) {
    auto phones=impl_->frontend(text);
    const auto length=phones.first.size();
    std::vector<std::int64_t> lengths{static_cast<std::int64_t>(length)}, sid{0}, langs(256,0);
    for(std::size_t i=1;i<length;i+=2) langs[i]=3;
    phones.first.resize(256); phones.second.resize(256);
    std::vector<float> bert(768*256,0), noise_w{.8f}, ratio{.2f}, noise{.6f};
    auto start=Clock::now();
    auto out=impl_->encoder.infer({input(phones.first,RKNN_TENSOR_INT64),input(lengths,RKNN_TENSOR_INT64),
        input(sid,RKNN_TENSOR_INT64),input(phones.second,RKNN_TENSOR_INT64),input(langs,RKNN_TENSOR_INT64),
        input(bert,RKNN_TENSOR_FLOAT32),input(noise_w,RKNN_TENSOR_FLOAT32),input(ratio,RKNN_TENSOR_FLOAT32)});
    std::cout<<"encoder_ms="<<ms(start)<<'\n'; start=Clock::now();
    std::vector<float> attn(512*256,0),mask(512,0);
    int total=0;
    for(int i=0;i<256;++i) {
        const auto duration=std::ceil(std::exp(out[0][i])*out[1][i]);
        if(!std::isfinite(duration)||duration<0||duration>512-total)
            throw std::runtime_error("audio exceeds 5.94 seconds or invalid duration; shorten text");
        const int end=total+static_cast<int>(duration);
        for(int j=total;j<end;++j) { attn[j*256+i]=out[1][i]; mask[j]=1; }
        total=end;
    }
    if(total==0) throw std::runtime_error("empty duration");
    std::cout<<"middle_ms="<<ms(start)<<'\n'; start=Clock::now();
    auto audio=impl_->decoder.infer({input(attn,RKNN_TENSOR_FLOAT32),input(mask,RKNN_TENSOR_FLOAT32),
        input(out[2],RKNN_TENSOR_FLOAT32),input(out[3],RKNN_TENSOR_FLOAT32),
        input(out[4],RKNN_TENSOR_FLOAT32),input(noise,RKNN_TENSOR_FLOAT32)});
    std::cout<<"decoder_ms="<<ms(start)<<'\n';
    audio[0].resize(total*512);
    double energy=0; float peak=0;
    for(float v:audio[0]) {
        if(!std::isfinite(v)) throw std::runtime_error("nonfinite waveform");
        energy+=v*v; peak=std::max(peak,std::abs(v));
    }
    const double rms=std::sqrt(energy/audio[0].size());
    if(rms<.00001 || peak>2) throw std::runtime_error("silent or invalid waveform");
    std::cout<<"waveform samples="<<audio[0].size()<<" rms="<<rms<<" peak="<<peak<<'\n';
    return std::move(audio[0]);
}
}
