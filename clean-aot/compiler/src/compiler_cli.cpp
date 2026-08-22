#include "clean_aot/compiler/cpp_emitter.hpp"
#include "clean_aot/compiler/region_builder.hpp"

#include <algorithm>
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

[[nodiscard]] std::uint64_t number(std::string_view text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(std::string(text), &consumed, 0);
    if (consumed != text.size()) throw std::runtime_error("invalid manifest number");
    return value;
}

[[nodiscard]] std::string symbol(GuestAddress address) {
    std::ostringstream out; out << "clean_aot_region_" << std::hex << std::setw(8)
                                << std::setfill('0') << address; return out.str();
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
    for (int i=1;i<argc;i+=2) {
        if (i+1>=argc) throw std::runtime_error("option lacks value");
        const std::string_view option(argv[i]);
        if (option=="--image" || option=="--exe") image=argv[i+1];
        else if (option=="--image-kind") kind=argv[i+1];
        else if (option=="--manifest") manifest=argv[i+1];
        else if (option=="--output") output=argv[i+1];
        else throw std::runtime_error("unknown option");
    }
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
    const auto bytes=read_bytes(image); if(bytes.size()!=end-load) throw std::runtime_error("image size does not match load range");
    const auto whole=clean_aot::compiler::PsxExeImage::parse_raw(bytes,static_cast<GuestAddress>(load),roots.front());
    if(std::filesystem::exists(output)) throw std::runtime_error("output already exists");
    std::filesystem::create_directories(output);
    struct Entry { GuestAddress pc; GuestAddress region; }; std::vector<Entry> entries;
    for(const Range range:ranges) {
        std::vector<GuestAddress> region_roots; std::copy_if(roots.begin(),roots.end(),std::back_inserter(region_roots),[&](auto root){return root>=range.start&&root<range.end;});
        if(region_roots.empty()) throw std::runtime_error("each code range requires an explicit entry");
        clean_aot::compiler::RegionBuildOptions options{region_roots,range.start,range.end};
        const auto region=clean_aot::compiler::RegionBuilder::build(whole,std::move(options));
        std::ofstream source(output/(symbol(range.start)+".cpp"),std::ios::binary); source<<clean_aot::compiler::CppEmitter::emit(region); if(!source) throw std::runtime_error("unable to write region source");
        for(const auto& block:region.blocks) entries.push_back({block.start,range.start});
    }
    std::sort(entries.begin(),entries.end(),[](auto a,auto b){return a.pc<b.pc;});
    if(std::adjacent_find(entries.begin(),entries.end(),[](auto a,auto b){return a.pc==b.pc;})!=entries.end()) throw std::runtime_error("overlapping generated entries");
    const std::string variant=one("variant_sha256"), digest=one("image_sha256");
    std::ofstream binding(output/"entry_table.cpp",std::ios::binary);
    binding<<"#include <array>\n#include <clean_aot/runtime/native_dispatch.hpp>\n\n";
    for(const auto& entry:entries) binding<<"extern \"C\" bool "<<symbol(entry.region)<<"(std::uint32_t&, bool) noexcept;\n";
    binding<<"namespace {\nconstexpr std::array<std::uint32_t,"<<roots.size()<<"> roots{{"; for(auto root:roots) binding<<"0x"<<std::hex<<root<<"U,";
    binding<<"}};\nconst std::array<clean_aot::runtime::GeneratedRegionEntry,"<<std::dec<<entries.size()<<"> entries{{";
    for(const auto& entry:entries) binding<<"{0x"<<std::hex<<entry.pc<<"U,"<<symbol(entry.region)<<"},";
    binding<<"}};\nconst clean_aot::runtime::GeneratedExecutableBinding value{\""<<variant<<"\",\""<<digest<<"\",0x"<<std::hex<<load<<"U,0x"<<end<<"ULL,roots,entries};\n}\n";
    binding<<"extern \"C\" const clean_aot::runtime::GeneratedExecutableBinding* clean_aot_binding_"<<variant<<"() noexcept { return &value; }\n";
    if(!binding) throw std::runtime_error("unable to write entry table");
    std::cout<<"generated "<<entries.size()<<" authenticated entries\n"; return 0;
}
}
int main(int argc,char** argv){try{return run(argc,argv);}catch(const std::exception& e){std::cerr<<"clean_aot_compiler_cli: "<<e.what()<<'\n';return 2;}}
