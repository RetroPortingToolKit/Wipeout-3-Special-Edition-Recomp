#include "clean_aot/compiler/cpp_emitter.hpp"
#include "clean_aot/compiler/region_builder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using clean_aot::compiler::GuestAddress;
struct Range { GuestAddress start; std::uint64_t end; };

constexpr std::array<std::uint32_t,64> sha_k{0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
void sha_block(std::array<std::uint32_t,8>& h,const std::uint8_t* p){std::array<std::uint32_t,64>w{};for(std::size_t i=0;i<16;i++)w[i]=(std::uint32_t{p[i*4]}<<24)|(std::uint32_t{p[i*4+1]}<<16)|(std::uint32_t{p[i*4+2]}<<8)|p[i*4+3];for(std::size_t i=16;i<64;i++){auto a=std::rotr(w[i-15],7)^std::rotr(w[i-15],18)^(w[i-15]>>3);auto b=std::rotr(w[i-2],17)^std::rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+a+w[i-7]+b;}auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],q=h[7];for(std::size_t i=0;i<64;i++){auto x=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);auto y=(e&f)^((~e)&g);auto t1=q+x+y+sha_k[i]+w[i];auto z=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);auto t2=z+((a&b)^(a&c)^(b&c));q=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=q;}
[[nodiscard]] std::string sha256(const std::vector<std::uint8_t>& bytes){std::array<std::uint32_t,8>h{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};std::size_t o=0;while(bytes.size()-o>=64){sha_block(h,bytes.data()+o);o+=64;}std::array<std::uint8_t,128>tail{};auto n=bytes.size()-o;std::copy(bytes.begin()+static_cast<std::ptrdiff_t>(o),bytes.end(),tail.begin());tail[n]=0x80;auto padded=n<56?64U:128U;auto bits=static_cast<std::uint64_t>(bytes.size())*8U;for(std::size_t i=0;i<8;i++)tail[padded-1-i]=static_cast<std::uint8_t>(bits>>(i*8));sha_block(h,tail.data());if(padded==128)sha_block(h,tail.data()+64);std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto word:h)out<<std::setw(8)<<word;return out.str();}

[[nodiscard]] std::uint64_t number(std::string_view text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(std::string(text), &consumed, 0);
    if (consumed != text.size()) throw std::runtime_error("invalid manifest number");
    return value;
}

[[nodiscard]] std::string symbol(GuestAddress address,
                                 std::string_view suffix = {}) {
    std::ostringstream out; out << "clean_aot_region_" << std::hex << std::setw(8)
                                << std::setfill('0') << address << suffix;
    return out.str();
}

[[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("unable to open image");
    const auto end = in.tellg();
    if (end <= 0) throw std::runtime_error("image is empty");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.seekg(0); in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in) throw std::runtime_error("unable to read complete image");
    return bytes;
}

int run(int argc, char** argv) {
    std::filesystem::path image, manifest, output; std::string kind;
    std::uint64_t max_shard_instructions=2048U;
    for (int i=1;i<argc;i+=2) {
        if (i+1>=argc) throw std::runtime_error("option lacks value");
        const std::string_view option(argv[i]);
        if (option=="--image" || option=="--exe") image=argv[i+1];
        else if (option=="--image-kind") kind=argv[i+1];
        else if (option=="--manifest") manifest=argv[i+1];
        else if (option=="--output") output=argv[i+1];
        else if (option=="--max-shard-instructions") max_shard_instructions=number(argv[i+1]);
        else throw std::runtime_error("unknown option");
    }
    if(max_shard_instructions==0U||max_shard_instructions>65535U) throw std::runtime_error("invalid maximum shard instruction count");
    if (image.empty() || manifest.empty() || output.empty()) throw std::runtime_error("missing required option");
    std::ifstream input(manifest); if (!input) throw std::runtime_error("unable to open manifest");
    std::map<std::string,std::vector<std::string>> fields; std::string line;
    while (std::getline(input,line)) { const auto split=line.find('='); if(split==std::string::npos) throw std::runtime_error("malformed manifest"); fields[line.substr(0,split)].push_back(line.substr(split+1)); }
    auto one=[&](const char* key)->const std::string& { const auto it=fields.find(key); if(it==fields.end()||it->second.size()!=1U) throw std::runtime_error(std::string("missing/duplicate manifest field: ")+key); return it->second[0]; };
    if (one("schema")!="clean-aot-compiler-manifest-v1") throw std::runtime_error("unsupported manifest schema");
    if (kind.empty()) kind=one("image_kind");
    if (kind!="raw-payload" && kind!="ps-x-exe") throw std::runtime_error("unsupported image kind");
    const auto load=number(one("load_start")); const auto end=number(one("load_end_exclusive"));
    if(load>0xffffffffULL||end<=load||end>(1ULL<<32U)) throw std::runtime_error("invalid load range");
    std::vector<GuestAddress> roots; for(const auto& value:fields["entry"]) roots.push_back(static_cast<GuestAddress>(number(value)));
    if(roots.empty()||!std::is_sorted(roots.begin(),roots.end())||std::adjacent_find(roots.begin(),roots.end())!=roots.end()) throw std::runtime_error("entries must be sorted and unique");
    std::vector<Range> ranges; for(const auto& value:fields["code"]) { const auto dash=value.find('-'); if(dash==std::string::npos) throw std::runtime_error("malformed code range"); ranges.push_back({static_cast<GuestAddress>(number(value.substr(0,dash))),number(value.substr(dash+1))}); }
    if(ranges.empty()) throw std::runtime_error("manifest has no code ranges");
    if(!std::is_sorted(ranges.begin(),ranges.end(),[](const Range& a,const Range& b){return a.start<b.start;})) throw std::runtime_error("code ranges must be sorted");
    for(std::size_t i=0;i<ranges.size();++i)if(ranges[i].end<=ranges[i].start||(i!=0U&&ranges[i].start<ranges[i-1U].end))throw std::runtime_error("code ranges must be nonempty and nonoverlapping");
    const auto bytes=read_bytes(image); if(bytes.size()!=end-load) throw std::runtime_error("image size does not match load range");
    const std::string digest=one("image_sha256");
    if (digest.size()!=64U || sha256(bytes)!=digest) throw std::runtime_error("image SHA-256 mismatch");
    const auto whole=clean_aot::compiler::PsxExeImage::parse_raw(bytes,static_cast<GuestAddress>(load),roots.front());
    const std::string variant=one("variant_sha256");
    const auto symbol_namespace=fields.find("symbol_namespace");
    if(symbol_namespace!=fields.end()&&(symbol_namespace->second.size()!=1U||
       symbol_namespace->second[0]!="variant"))throw std::runtime_error("unsupported symbol namespace");
    const std::string symbol_suffix=symbol_namespace==fields.end()
        ? std::string{} : std::string("_v")+variant;
    if(std::filesystem::exists(output)) throw std::runtime_error("output already exists");
    std::filesystem::create_directories(output);
    struct Entry { GuestAddress pc; GuestAddress region; std::uint32_t instruction_count; }; std::vector<Entry> entries;
    struct Shard { GuestAddress region; GuestAddress identity; std::uint32_t block_count; std::uint32_t instruction_count; std::string source_sha256; }; std::vector<Shard> shards;
    std::set<GuestAddress> shard_identities;
    for(const Range range:ranges) {
        std::vector<GuestAddress> region_roots; std::copy_if(roots.begin(),roots.end(),std::back_inserter(region_roots),[&](auto root){return root>=range.start&&root<range.end;});
        if(region_roots.empty()) throw std::runtime_error("each code range requires an explicit entry");
        clean_aot::compiler::RegionBuildOptions options{region_roots,range.start,range.end};
        const auto region=clean_aot::compiler::RegionBuilder::build(whole,std::move(options));
        std::vector<const clean_aot::compiler::BasicBlock*> blocks; for(const auto& block:region.blocks)blocks.push_back(&block);
        std::sort(blocks.begin(),blocks.end(),[](const auto* a,const auto* b){return a->start<b->start||(a->start==b->start&&a->id<b->id);});
        std::size_t begin=0;
        while(begin<blocks.size()){
            std::size_t finish=begin;std::uint64_t instructions=0;
            while(finish<blocks.size()&&(finish==begin||instructions+blocks[finish]->instructions.size()<=max_shard_instructions)){instructions+=blocks[finish]->instructions.size();++finish;}
            const GuestAddress shard_identity=blocks[begin]->start;
            if(!shard_identities.insert(shard_identity).second)throw std::runtime_error("duplicate generated shard identity");
            std::vector<clean_aot::compiler::BlockId> ids;ids.reserve(finish-begin);
            for(std::size_t i=begin;i<finish;++i){ids.push_back(blocks[i]->id);entries.push_back({blocks[i]->start,shard_identity,static_cast<std::uint32_t>(blocks[i]->instructions.size())});}
            const std::string generated=clean_aot::compiler::CppEmitter::emit_shard(region,ids,shard_identity,symbol_suffix);
            std::ofstream source(output/(symbol(shard_identity,symbol_suffix)+".cpp"),std::ios::binary);source<<generated;if(!source)throw std::runtime_error("unable to write shard source");
            const std::vector<std::uint8_t> generated_bytes(generated.begin(),generated.end());
            shards.push_back({range.start,shard_identity,static_cast<std::uint32_t>(ids.size()),static_cast<std::uint32_t>(instructions),sha256(generated_bytes)});
            begin=finish;
        }
    }
    std::sort(entries.begin(),entries.end(),[](auto a,auto b){return a.pc<b.pc;});
    if(std::adjacent_find(entries.begin(),entries.end(),[](auto a,auto b){return a.pc==b.pc;})!=entries.end()) throw std::runtime_error("overlapping generated entries");
    std::vector<GuestAddress> regions; for(const auto& entry:entries) regions.push_back(entry.region);
    std::sort(regions.begin(),regions.end()); regions.erase(std::unique(regions.begin(),regions.end()),regions.end());
    if(regions.size()>65536U) throw std::runtime_error("too many generated regions for packed table");
    for(const auto& entry:entries) if(entry.instruction_count>65535U) throw std::runtime_error("generated block is too large for packed table");
    const auto first_page=load>>12U; const auto last_page=(end-1U)>>12U;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> pages;
    std::size_t cursor=0;
    for(auto page=first_page;page<=last_page;++page){const auto begin=cursor;while(cursor<entries.size()&&(entries[cursor].pc>>12U)==page)++cursor;pages.emplace_back(static_cast<std::uint32_t>(begin),static_cast<std::uint32_t>(cursor));}
    std::ofstream binding(output/"entry_table.cpp",std::ios::binary);
    binding<<"#include <array>\n#include <clean_aot/runtime/native_dispatch.hpp>\n\n";
    for(const auto region:regions) binding<<"extern \"C\" bool "<<symbol(region,symbol_suffix)<<"(clean_aot::runtime::GeneratedCpuState&) noexcept;\n";
    binding<<"namespace {\nconstexpr std::array<std::uint32_t,"<<roots.size()<<"> roots{{"; for(auto root:roots) binding<<"0x"<<std::hex<<root<<"U,";
    binding<<"}};\nconst std::array<clean_aot::runtime::GeneratedRegionFunction,"<<std::dec<<regions.size()<<"> regions{{"; for(auto region:regions) binding<<symbol(region,symbol_suffix)<<",";
    binding<<"}};\nconstexpr std::array<clean_aot::runtime::GeneratedPackedRegionEntry,"<<entries.size()<<"> entries{{";
    for(const auto& entry:entries){const auto index=std::lower_bound(regions.begin(),regions.end(),entry.region)-regions.begin();binding<<"{0x"<<std::hex<<entry.pc<<"U,"<<std::dec<<index<<"U,"<<entry.instruction_count<<"U},";}
    binding<<"}};\nconstexpr std::array<clean_aot::runtime::GeneratedDispatchPage,"<<pages.size()<<"> pages{{";for(const auto [begin_index,end_index]:pages)binding<<"{"<<begin_index<<"U,"<<end_index<<"U},";
    binding<<"}};\nconst clean_aot::runtime::GeneratedExecutableBinding value{\""<<variant<<"\",\""<<digest<<"\",0x"<<std::hex<<load<<"U,0x"<<end<<"ULL,roots,{},regions,entries,pages};\n}\n";
    binding<<"extern \"C\" const clean_aot::runtime::GeneratedExecutableBinding* clean_aot_binding_"<<variant<<"() noexcept { return &value; }\n";
    binding.close(); if(!binding) throw std::runtime_error("unable to write entry table");
    const std::string entry_table_sha256=sha256(read_bytes(output/"entry_table.cpp"));
    std::ofstream shard_manifest(output/"shards.manifest",std::ios::binary);
    shard_manifest<<"schema=clean-aot-generated-shards-v1\nvariant_sha256="<<variant<<"\nimage_sha256="<<digest<<"\nentry_table_sha256="<<entry_table_sha256<<"\nmax_shard_instructions="<<max_shard_instructions<<"\nentry_count="<<entries.size()<<"\nshard_count="<<shards.size()<<"\n";
    for(const auto& shard:shards)shard_manifest<<"shard=0x"<<std::hex<<shard.region<<",0x"<<shard.identity<<","<<std::dec<<shard.block_count<<","<<shard.instruction_count<<","<<shard.source_sha256<<"\n";
    if(!shard_manifest)throw std::runtime_error("unable to write shard manifest");
    std::cout<<"generated "<<entries.size()<<" authenticated entries in "<<shards.size()<<" shards\n"; return 0;
}
}
int main(int argc,char** argv){try{return run(argc,argv);}catch(const std::exception& e){std::cerr<<"clean_aot_compiler_cli: "<<e.what()<<'\n';return 2;}}
