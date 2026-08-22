#include "clean_aot/app/authorization.hpp"

#include "json.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <span>

namespace clean_aot::app {
namespace {
using detail::Json;

Authorization rejected(AuthorizationCode code, std::string detail) {
    Authorization result{}; result.code=code; result.detail=std::move(detail); return result;
}

bool read_file(const std::filesystem::path& path, std::size_t limit,
               std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    const auto size=std::filesystem::file_size(path,error);
    if(error || size>limit || size>std::numeric_limits<std::size_t>::max())return false;
    bytes.resize(static_cast<std::size_t>(size));
    std::ifstream stream(path,std::ios::binary);
    return stream.good() && (bytes.empty() || static_cast<bool>(stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))));
}

bool document(const std::filesystem::path& path, Json& value, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if(!read_file(path,16U*1024U*1024U,bytes)){error="cannot read bounded JSON input";return false;}
    return detail::parse_json(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()),value,error);
}

const Json* at(const Json& root, std::initializer_list<std::string_view> path) {
    const Json* current=&root; for(const auto key:path){current=detail::member(*current,key);if(current==nullptr)return nullptr;} return current;
}
const std::string* text(const Json& root, std::initializer_list<std::string_view> path) { const Json* v=at(root,path);return v==nullptr?nullptr:v->string(); }
const std::uint64_t* number(const Json& root, std::initializer_list<std::string_view> path) { const Json* v=at(root,path);return v==nullptr?nullptr:v->number(); }
bool equals(const Json& root,std::initializer_list<std::string_view> path,std::string_view expected){const auto* value=text(root,path);return value!=nullptr&&*value==expected;}
bool digest(std::string_view value){return value.size()==64U&&std::all_of(value.begin(),value.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}

Json identity_of(const Json& variant) {
    detail::JsonObject object;
    object.emplace("load_range",*detail::member(variant,"load_range"));
    object.emplace("relocation_metadata",*detail::member(variant,"relocation_metadata"));
    object.emplace("sha256",*detail::member(variant,"sha256"));
    return Json{std::move(object)};
}

bool safe_artifact(const std::filesystem::path& root,const std::string& relative,
                   std::filesystem::path& resolved) {
    const std::filesystem::path rel=std::filesystem::path(relative);
    if(rel.empty()||rel.is_absolute()||rel.has_root_name())return false;
    for(const auto& part:rel)if(part==".."||part==".")return false;
    std::error_code error; const auto canonical_root=std::filesystem::canonical(root,error);if(error)return false;
    resolved=std::filesystem::weakly_canonical(canonical_root/rel,error);if(error)return false;
    auto root_it=canonical_root.begin();auto file_it=resolved.begin();
    for(;root_it!=canonical_root.end();++root_it,++file_it)if(file_it==resolved.end()||*root_it!=*file_it)return false;
    return true;
}

bool address_range(const Json& value,std::uint64_t& start,std::uint64_t& end){
    const Json* s=detail::member(value,"start");const Json* e=detail::member(value,"end_exclusive");
    if(s==nullptr||e==nullptr||s->number()==nullptr||e->number()==nullptr)return false;
    start=*s->number();end=*e->number();return start<end&&end<=(std::uint64_t{1}<<32U);
}
} // namespace

Authorization authorize_launch(const LaunchInputs& inputs,
                               const runtime::GeneratedExecutableBinding& binding) noexcept {
    try {
        Json setup,finalize,catalog,selection;std::string error;
        if(!document(inputs.setup_receipt,setup,error)||!document(inputs.finalize_receipt,finalize,error)||
           !document(inputs.catalog,catalog,error)||!document(inputs.selection_receipt,selection,error))
            return rejected(AuthorizationCode::InvalidDocument,error);
        if(!equals(setup,{"schema"},"psxrecomp.clean-aot.product-receipt.v1")||!equals(setup,{"status"},"setup-complete")||
           !equals(finalize,{"schema"},"psxrecomp.clean-aot.product-finalize-receipt.v1")||!equals(finalize,{"status"},"catalog-finalized")||
           !equals(selection,{"schema"},"psxrecomp.clean-aot.selection.v1")||!equals(selection,{"status"},"selected")||
           !equals(catalog,{"schema"},"psxrecomp.clean-aot.catalog.v1")||
           !equals(finalize,{"catalog_path"},"catalog.json")||
           !equals(finalize,{"runtime_policy","unknown_variant_behavior"},"reject-before-dispatch"))
            return rejected(AuthorizationCode::NotFinalized,"setup/catalog selection is not finalized");

        const auto* setup_input_hash=text(setup,{"catalog","input_sha256"});
        const auto* finalized_input_hash=text(finalize,{"catalog_input_sha256"});
        if(setup_input_hash==nullptr||finalized_input_hash==nullptr||!digest(*setup_input_hash)||*setup_input_hash!=*finalized_input_hash)
            return rejected(AuthorizationCode::NotFinalized,"finalized catalog is not bound to this setup receipt");

        const std::string catalog_canonical=detail::canonical_json(catalog);
        const std::string catalog_hash=detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(catalog_canonical.data()),catalog_canonical.size()));
        const auto* final_hash=text(finalize,{"catalog_sha256"});const auto* selected_hash=text(selection,{"catalog_sha256"});
        if(final_hash==nullptr||selected_hash==nullptr||!digest(*final_hash)||catalog_hash!=*final_hash||catalog_hash!=*selected_hash)
            return rejected(AuthorizationCode::MeasurementMismatch,"catalog hash does not match finalized selection");

        const auto* selected_identity=text(selection,{"variant_identity_sha256"});
        const Json* variants=detail::member(catalog,"variants");
        if(selected_identity==nullptr||!digest(*selected_identity)||variants==nullptr||variants->array()==nullptr)
            return rejected(AuthorizationCode::InvalidDocument,"selection or catalog variants are malformed");
        const Json* variant=nullptr;
        for(const Json& candidate:*variants->array()){
            if(candidate.object()==nullptr||detail::member(candidate,"load_range")==nullptr||detail::member(candidate,"sha256")==nullptr||detail::member(candidate,"relocation_metadata")==nullptr)continue;
            const std::string canonical=detail::canonical_json(identity_of(candidate));
            const std::string identity=detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()));
            if(identity==*selected_identity){variant=&candidate;break;}
        }
        if(variant==nullptr)return rejected(AuthorizationCode::IdentityMismatch,"selected identity is absent from finalized catalog");

        const Json* selected_range=at(selection,{"binding","load_range"});
        const Json* selected_roots=at(selection,{"binding","dispatch_roots"});
        const Json* selected_executed=at(selection,{"binding","executed_ranges"});
        const Json* variant_range=detail::member(*variant,"load_range");
        const Json* variant_roots=detail::member(*variant,"dispatch_roots");
        const Json* variant_executed=detail::member(*variant,"executed_ranges");
        if(selected_range==nullptr||selected_roots==nullptr||selected_executed==nullptr||
           variant_range==nullptr||variant_roots==nullptr||variant_executed==nullptr||
           detail::canonical_json(*selected_range)!=detail::canonical_json(*variant_range)||
           detail::canonical_json(*selected_roots)!=detail::canonical_json(*variant_roots)||
           detail::canonical_json(*selected_executed)!=detail::canonical_json(*variant_executed))
            return rejected(AuthorizationCode::IdentityMismatch,"selection binding differs from finalized catalog");

        std::uint64_t load_start=0U,load_end=0U;
        const Json* range=detail::member(*variant,"load_range");
        if(range==nullptr||!address_range(*range,load_start,load_end)||binding.variant_identity_sha256!=*selected_identity||
           binding.load_start!=load_start||binding.load_end_exclusive!=load_end||binding.entries.empty()||binding.dispatch_roots.empty())
            return rejected(AuthorizationCode::BindingMismatch,"compiled generated binding does not match selection");

        std::vector<std::uint8_t> executable;
        if(!read_file(inputs.executable,512U*1024U*1024U,executable))return rejected(AuthorizationCode::IoError,"cannot read bounded selected executable");
        const auto* executable_hash=text(*variant,{"sha256"});
        if(executable_hash==nullptr||binding.executable_sha256!=*executable_hash||executable.size()!=load_end-load_start||detail::sha256(executable)!=*executable_hash)
            return rejected(AuthorizationCode::MeasurementMismatch,"selected executable bytes do not match catalog");

        const Json* artifact=detail::member(*variant,"artifact");
        const auto* artifact_id=artifact==nullptr?nullptr:text(*artifact,{"id"});const auto* artifact_path=artifact==nullptr?nullptr:text(*artifact,{"path"});const auto* artifact_hash=artifact==nullptr?nullptr:text(*artifact,{"sha256"});
        const auto* selected_id=text(selection,{"artifact","id"});const auto* selected_path=text(selection,{"artifact","path"});const auto* selected_artifact_hash=text(selection,{"artifact","sha256"});const auto* selected_size=number(selection,{"artifact","size"});
        std::filesystem::path resolved;
        if(artifact_id==nullptr||artifact_path==nullptr||artifact_hash==nullptr||selected_id==nullptr||selected_path==nullptr||selected_artifact_hash==nullptr||selected_size==nullptr||
           *artifact_id!=*selected_id||*artifact_path!=*selected_path||*artifact_hash!=*selected_artifact_hash||!safe_artifact(inputs.artifact_root,*artifact_path,resolved))
            return rejected(AuthorizationCode::UnsafePath,"selected artifact path or identity is unsafe");
        std::vector<std::uint8_t> artifact_bytes;
        if(!read_file(resolved,512U*1024U*1024U,artifact_bytes)||artifact_bytes.size()!=*selected_size||detail::sha256(artifact_bytes)!=*artifact_hash)
            return rejected(AuthorizationCode::MeasurementMismatch,"generated artifact bytes do not match catalog");

        const auto* bios_size=number(setup,{"media","bios","size"});const auto* bios_hash=text(setup,{"media","bios","sha256"});
        std::vector<std::uint8_t> bios;
        if(bios_size==nullptr||bios_hash==nullptr||*bios_size>16U*1024U*1024U||!read_file(inputs.bios,static_cast<std::size_t>(*bios_size),bios)||bios.size()!=*bios_size||detail::sha256(bios)!=*bios_hash)
            return rejected(AuthorizationCode::MeasurementMismatch,"BIOS bytes do not match authenticated setup receipt");

        const Json* executed=detail::member(*variant,"executed_ranges");const Json* roots=detail::member(*variant,"dispatch_roots");
        if(executed==nullptr||executed->array()==nullptr||roots==nullptr||roots->array()==nullptr)return rejected(AuthorizationCode::InvalidDocument,"catalog binding ranges are malformed");
        for(const auto& entry:binding.entries){
            bool covered=false;for(const Json& item:*executed->array()){std::uint64_t start=0U,end=0U;if(address_range(item,start,end)&&entry.pc>=start&&entry.pc<end){covered=true;break;}}
            if(!covered)return rejected(AuthorizationCode::BindingMismatch,"generated entry is outside authenticated execution ranges");
        }
        for(const Json& root:*roots->array()){
            if(root.number()==nullptr||*root.number()>std::numeric_limits<std::uint32_t>::max()||
               std::none_of(binding.entries.begin(),binding.entries.end(),[&](const auto& entry){return entry.pc==*root.number();}))
                return rejected(AuthorizationCode::BindingMismatch,"authenticated dispatch root is absent from generated binding");
        }
        if(binding.dispatch_roots.size()!=roots->array()->size())return rejected(AuthorizationCode::BindingMismatch,"compiled dispatch roots do not exactly match catalog");
        for(std::size_t index=0U;index<binding.dispatch_roots.size();++index)if(roots->array()->at(index).number()==nullptr||binding.dispatch_roots[index]!=*roots->array()->at(index).number())return rejected(AuthorizationCode::BindingMismatch,"compiled dispatch root order differs from catalog");

        Authorization result{};result.code=AuthorizationCode::Authorized;result.detail="authenticated finalized catalog selection";result.variant_identity_sha256=*selected_identity;result.bios=std::move(bios);return result;
    } catch (...) { return rejected(AuthorizationCode::InvalidDocument,"authorization failed closed"); }
}
} // namespace clean_aot::app
