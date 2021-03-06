#include "msg.h"
#include "utils.hpp"
#include "sysutils.hpp"
#include "farutils.hpp"
#include "common.hpp"
#include "ui.hpp"
#include "archive.hpp"
#include "options.hpp"

const wchar_t* c_method_copy   = L"Copy";

const wchar_t* c_method_lzma   = L"LZMA";
const wchar_t* c_method_lzma2  = L"LZMA2";
const wchar_t* c_method_ppmd   = L"PPMD";

const wchar_t* c_method_lzham  = L"LZHAM";
const wchar_t* c_method_zstd   = L"ZSTD";
const wchar_t* c_method_lz4    = L"LZ4";
const wchar_t* c_method_lz5    = L"LZ5";
const wchar_t* c_method_brotli = L"BROTLI";
const wchar_t* c_method_lizard = L"LIZARD";

#define DEFINE_ARC_ID(name, value) \
  const unsigned char c_guid_##name[] = "\x69\x0F\x17\x23\xC1\x40\x8A\x27\x10\x00\x00\x01\x10" value "\x00\x00"; \
  const ArcType c_##name(c_guid_##name, c_guid_##name + 16);

DEFINE_ARC_ID(7z, "\x07")
DEFINE_ARC_ID(zip, "\x01")
DEFINE_ARC_ID(bzip2, "\x02")
DEFINE_ARC_ID(gzip, "\xEF")
DEFINE_ARC_ID(xz, "\x0C")
DEFINE_ARC_ID(iso, "\xE7")
DEFINE_ARC_ID(udf, "\xE0")
DEFINE_ARC_ID(rar, "\x03")
DEFINE_ARC_ID(split, "\xEA")
DEFINE_ARC_ID(wim, "\xE6")
DEFINE_ARC_ID(tar, "\xEE")
DEFINE_ARC_ID(SWFc, "\xD8")

#undef DEFINE_ARC_ID

const unsigned __int64 c_min_volume_size = 16 * 1024;

const wchar_t* c_sfx_ext = L".exe";
const wchar_t* c_volume_ext = L".001";

unsigned Archive::max_check_size;

HRESULT ArcLib::get_prop(UInt32 index, PROPID prop_id, PROPVARIANT* prop) const {
  if (GetHandlerProperty2) {
    return GetHandlerProperty2(index, prop_id, prop);
  }
  else {
    assert(index == 0);
    return GetHandlerProperty(prop_id, prop);
  }
}

HRESULT ArcLib::get_bool_prop(UInt32 index, PROPID prop_id, bool& value) const {
  PropVariant prop;
  HRESULT res = get_prop(index, prop_id, prop.ref());
  if (res != S_OK)
    return res;
  if (!prop.is_bool())
    return E_FAIL;
  value = prop.get_bool();
  return S_OK;
}

HRESULT ArcLib::get_uint_prop(UInt32 index, PROPID prop_id, UInt32& value) const {
  PropVariant prop;
  HRESULT res = get_prop(index, prop_id, prop.ref());
  if (res != S_OK)
    return res;
  if (!prop.is_uint())
    return E_FAIL;
  value = (UInt32)prop.get_uint();
  return S_OK;
}

HRESULT ArcLib::get_string_prop(UInt32 index, PROPID prop_id, wstring& value) const {
  PropVariant prop;
  HRESULT res = get_prop(index, prop_id, prop.ref());
  if (res != S_OK)
    return res;
  if (!prop.is_str())
    return E_FAIL;
  value = prop.get_str();
  return S_OK;
}

HRESULT ArcLib::get_bytes_prop(UInt32 index, PROPID prop_id, ByteVector& value) const {
  PropVariant prop;
  HRESULT res = get_prop(index, prop_id, prop.ref());
  if (res != S_OK)
    return res;
  if (prop.vt == VT_BSTR) {
    UINT len = SysStringByteLen(prop.bstrVal);
    unsigned char* data =  reinterpret_cast<unsigned char*>(prop.bstrVal);
    value.assign(data, data + len);
  }
  else
    return E_FAIL;
  return S_OK;
}


wstring ArcFormat::default_extension() const {
  if (extension_list.empty())
    return wstring();
  else
    return extension_list.front();
}

ArcTypes ArcFormats::get_arc_types() const {
  ArcTypes types;
  for (const_iterator fmt = begin(); fmt != end(); fmt++) {
    types.push_back(fmt->first);
  }
  return types;
}

ArcTypes ArcFormats::find_by_name(const wstring& name) const {
  ArcTypes types;
  wstring uc_name = upcase(name);
  for (const_iterator fmt = begin(); fmt != end(); fmt++) {
    if (upcase(fmt->second.name) == uc_name)
      types.push_back(fmt->first);
  }
  return types;
}

ArcTypes ArcFormats::find_by_ext(const wstring& ext) const {
  ArcTypes types;
  if (ext.empty())
    return types;
  wstring uc_ext = upcase(ext);
  for (const_iterator fmt = begin(); fmt != end(); fmt++) {
    for (auto ext_iter = fmt->second.extension_list.cbegin(); ext_iter != fmt->second.extension_list.cend(); ext_iter++) {
      if (upcase(*ext_iter) == uc_ext) {
        types.push_back(fmt->first);
        break;
      }
    }
  }
  return types;
}


uintptr_t SfxModules::find_by_name(const wstring& name) const {
  for (const_iterator sfx_module = begin(); sfx_module != end(); sfx_module++) {
    if (upcase(extract_file_name(sfx_module->path)) == upcase(name))
      return distance(begin(), sfx_module);
  }
  return size();
}


wstring ArcChain::to_string() const {
  wstring result;
  for_each(begin(), end(), [&] (const ArcEntry& arc) {
    if (!result.empty())
      result += L"\x2192";
    result += ArcAPI::formats().at(arc.type).name;
  });
  return result;
}

static bool GetCoderInfo(Func_GetMethodProperty getMethodProperty, UInt32 index, CDllCodecInfo& info) {
  info.DecoderIsAssigned = info.EncoderIsAssigned = false;
  std::fill((char *)&info.Decoder, sizeof(info.Decoder) + (char *)&info.Decoder, 0);
  info.Encoder = info.Decoder;
  PropVariant prop1, prop2, prop3, prop4;
  if (S_OK != getMethodProperty(index, NMethodPropID::kDecoder, prop1.ref()))
    return false;
  if (prop1.vt != VT_EMPTY) {
    if (prop1.vt != VT_BSTR || (size_t)SysStringByteLen(prop1.bstrVal) < sizeof(CLSID))
      return false;
    info.Decoder = *(const GUID *)prop1.bstrVal;
    info.DecoderIsAssigned = true;
  }
  if (S_OK != getMethodProperty(index, NMethodPropID::kEncoder, prop2.ref()))
    return false;
  if (prop2.vt != VT_EMPTY) {
    if (prop2.vt != VT_BSTR || (size_t)SysStringByteLen(prop2.bstrVal) < sizeof(CLSID))
      return false;
    info.Encoder = *(const GUID *)prop2.bstrVal;
    info.EncoderIsAssigned = true;
  }
  if (S_OK != getMethodProperty(index, NMethodPropID::kName, prop3.ref()) || !prop3.is_str())
    return false;
  info.Name = prop3.get_str();
  if (S_OK != getMethodProperty(index, NMethodPropID::kID, prop4.ref()) || !prop4.is_uint())
    return false;
  info.CodecId = static_cast<UInt32>(prop4.get_uint());
  return info.DecoderIsAssigned || info.EncoderIsAssigned;
}

class MyCompressCodecsInfo : public ICompressCodecsInfo, private ComBase {
private:
  const ArcLibs& libs_;
  const vector<CDllCodecInfo> &x_codecs_;
  vector <CDllCodecInfo> base_codecs_;
  UInt32 n_base_codecs_;
public:
  MyCompressCodecsInfo(const ArcLibs& libs, const vector<CDllCodecInfo> &codecs, size_t skip_lib_index)
   : libs_(libs), x_codecs_(codecs), n_base_codecs_(0)
  {
    size_t n_formats = codecs.empty() ? libs.size() : codecs[0].LibIndex;
    for (size_t ilib = 0; ilib < n_formats; ++ilib) {
      if (ilib == skip_lib_index)
        continue;
      const auto& arc_lib = libs[ilib];
      if ((arc_lib.CreateObject || arc_lib.CreateDecoder || arc_lib.CreateEncoder) && arc_lib.GetMethodProperty) {
        UInt32 numMethods = 1;
        bool ok = true;
        if (arc_lib.GetNumberOfMethods)
			 ok = S_OK == arc_lib.GetNumberOfMethods(&numMethods);
        for (UInt32 i = 0; ok && i < numMethods; ++i) {
          CDllCodecInfo info;
          info.LibIndex = static_cast<UInt32>(ilib);
          info.CodecIndex = i;
          if (GetCoderInfo(arc_lib.GetMethodProperty, i, info)) {
            base_codecs_.push_back(info);
            ++n_base_codecs_;
          }
        }
      }
    }
  }

  ~MyCompressCodecsInfo() {}

  UNKNOWN_IMPL_BEGIN
  UNKNOWN_IMPL_ITF(ICompressCodecsInfo)
  UNKNOWN_IMPL_END

  STDMETHODIMP GetNumMethods(UInt32 *numMethods) {
    *numMethods = static_cast<UInt32>(base_codecs_.size() + x_codecs_.size());
    return S_OK;
  }

  STDMETHODIMP GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value) {
    const CDllCodecInfo &ci = index < n_base_codecs_ ? base_codecs_[index] : x_codecs_[index-n_base_codecs_];
    if (propID == NMethodPropID::kDecoderIsAssigned || propID == NMethodPropID::kEncoderIsAssigned) {
      PropVariant prop;
      prop = (bool)((propID == NMethodPropID::kDecoderIsAssigned) ? ci.DecoderIsAssigned : ci.EncoderIsAssigned);
      prop.detach(value);
      return S_OK;
    }
    const auto &lib = libs_[ci.LibIndex];
    return lib.GetMethodProperty(ci.CodecIndex, propID, value);
  }

  STDMETHODIMP CreateDecoder(UInt32 index, const GUID *iid, void **coder) {
    const CDllCodecInfo &ci = index < n_base_codecs_ ? base_codecs_[index] : x_codecs_[index-n_base_codecs_];
    if (ci.DecoderIsAssigned) {
      const auto &lib = libs_[ci.LibIndex];
      if (lib.CreateDecoder)
        return lib.CreateDecoder(ci.CodecIndex, iid, (void **)coder);
      else
        return lib.CreateObject(&ci.Decoder, iid, (void **)coder);
    }
    return S_OK;
  }

  STDMETHODIMP CreateEncoder(UInt32 index, const GUID *iid, void **coder) {
    const CDllCodecInfo &ci = index < n_base_codecs_ ? base_codecs_[index] : x_codecs_[index-n_base_codecs_];
    if (ci.EncoderIsAssigned) {
      const auto &lib = libs_[ci.LibIndex];
      if (lib.CreateEncoder)
        return lib.CreateEncoder(ci.CodecIndex, iid, (void **)coder);
      else
        return lib.CreateObject(&ci.Encoder, iid, (void **)coder);
    }
    return S_OK;
  }
};

ArcAPI* ArcAPI::arc_api = nullptr;

ArcAPI::~ArcAPI() {
  for (size_t i = 0; i < n_format_libs; ++i) {
    if (arc_libs[i].SetCodecs)
      arc_libs[i].SetCodecs(nullptr); // calls ~MyCompressInfo()
  }
  compress_info.clear();
  for_each(arc_libs.begin(), arc_libs.end(), [&] (const ArcLib& arc_lib) {
    if (arc_lib.h_module)
      FreeLibrary(arc_lib.h_module);
  });
}

ArcAPI* ArcAPI::get() {
  if (arc_api == nullptr) {
    arc_api = new ArcAPI();
    arc_api->load();
    Patch7zCP::SetCP(static_cast<UINT>(g_options.oemCP), static_cast<UINT>(g_options.ansiCP));
  }
  return arc_api;
}

void ArcAPI::load_libs(const wstring& path) {
  FileEnum file_enum(path);
  wstring dir = extract_file_path(path);
  bool more;
  while (file_enum.next_nt(more) && more) {
    ArcLib arc_lib;
    arc_lib.module_path = add_trailing_slash(dir) + file_enum.data().cFileName;
    arc_lib.h_module = LoadLibraryW(arc_lib.module_path.c_str());
    if (arc_lib.h_module == nullptr)
      continue;
    arc_lib.CreateObject = reinterpret_cast<Func_CreateObject>(GetProcAddress(arc_lib.h_module, "CreateObject"));
    arc_lib.CreateDecoder = reinterpret_cast<Func_CreateDecoder>(GetProcAddress(arc_lib.h_module, "CreateDecoder"));
    arc_lib.CreateEncoder = reinterpret_cast<Func_CreateEncoder>(GetProcAddress(arc_lib.h_module, "CreateEncoder"));
    arc_lib.GetNumberOfMethods = reinterpret_cast<Func_GetNumberOfMethods>(GetProcAddress(arc_lib.h_module, "GetNumberOfMethods"));
    arc_lib.GetMethodProperty = reinterpret_cast<Func_GetMethodProperty>(GetProcAddress(arc_lib.h_module, "GetMethodProperty"));
    arc_lib.GetNumberOfFormats = reinterpret_cast<Func_GetNumberOfFormats>(GetProcAddress(arc_lib.h_module, "GetNumberOfFormats"));
    arc_lib.GetHandlerProperty = reinterpret_cast<Func_GetHandlerProperty>(GetProcAddress(arc_lib.h_module, "GetHandlerProperty"));
    arc_lib.GetHandlerProperty2 = reinterpret_cast<Func_GetHandlerProperty2>(GetProcAddress(arc_lib.h_module, "GetHandlerProperty2"));
    arc_lib.GetIsArc = reinterpret_cast<Func_GetIsArc>(GetProcAddress(arc_lib.h_module, "GetIsArc"));
    arc_lib.SetCodecs = reinterpret_cast<Func_SetCodecs>(GetProcAddress(arc_lib.h_module, "SetCodecs"));
    if (arc_lib.CreateObject && ((arc_lib.GetNumberOfFormats && arc_lib.GetHandlerProperty2) || arc_lib.GetHandlerProperty)) {
      arc_lib.version = get_module_version(arc_lib.module_path);
      arc_libs.push_back(arc_lib);
    }
    else
      FreeLibrary(arc_lib.h_module);
  }
}

void ArcAPI::load_codecs(const wstring& path) {
  if (n_base_format_libs <= 0)
    return;

  FileEnum codecs_enum(path);
  wstring dir = extract_file_path(path);
  bool more;
  while (codecs_enum.next_nt(more) && more && !codecs_enum.data().is_dir()) {
    ArcLib arc_lib;
    arc_lib.module_path = add_trailing_slash(dir) + codecs_enum.data().cFileName;
    arc_lib.h_module = LoadLibraryW(arc_lib.module_path.c_str());
    if (arc_lib.h_module == nullptr)
      continue;
    arc_lib.CreateObject = reinterpret_cast<Func_CreateObject>(GetProcAddress(arc_lib.h_module, "CreateObject"));
    arc_lib.CreateDecoder = reinterpret_cast<Func_CreateDecoder>(GetProcAddress(arc_lib.h_module, "CreateDecoder"));
    arc_lib.CreateEncoder = reinterpret_cast<Func_CreateEncoder>(GetProcAddress(arc_lib.h_module, "CreateEncoder"));
    arc_lib.GetNumberOfMethods = reinterpret_cast<Func_GetNumberOfMethods>(GetProcAddress(arc_lib.h_module, "GetNumberOfMethods"));
    arc_lib.GetMethodProperty = reinterpret_cast<Func_GetMethodProperty>(GetProcAddress(arc_lib.h_module, "GetMethodProperty"));
    arc_lib.GetNumberOfFormats = nullptr;
    arc_lib.GetHandlerProperty = nullptr;
    arc_lib.GetHandlerProperty2 = nullptr;
    arc_lib.GetIsArc = nullptr;
    arc_lib.SetCodecs = nullptr;
    arc_lib.version = 0;
    auto n_start_codecs = arc_codecs.size();
    if ((arc_lib.CreateObject || arc_lib.CreateDecoder || arc_lib.CreateEncoder) && arc_lib.GetMethodProperty) {
      UInt32 numMethods = 1;
      bool ok = true;
      if (arc_lib.GetNumberOfMethods)
        ok = S_OK == arc_lib.GetNumberOfMethods(&numMethods);
      for (UInt32 i = 0; ok && i < numMethods; ++i) {
        CDllCodecInfo info;
        info.LibIndex = static_cast<UInt32>(arc_libs.size());
        info.CodecIndex = i;
        if (GetCoderInfo(arc_lib.GetMethodProperty, i, info))
          arc_codecs.push_back(info);
      }
    }
    if (n_start_codecs < arc_codecs.size())
      arc_libs.push_back(arc_lib);
    else
      FreeLibrary(arc_lib.h_module);
  }

  n_7z_codecs = 0;
  if (arc_codecs.size() > 0) {
    std::sort(arc_codecs.begin(), arc_codecs.end(), [&](const auto&a, const auto& b) {
      bool a_is_zip = (a.CodecId & 0xffffff00U) == 0x040100;
      bool b_is_zip = (b.CodecId & 0xffffff00U) == 0x040100;
      if (a_is_zip != b_is_zip)
        return b_is_zip;
      else
        return _wcsicmp(a.Name.data(), b.Name.data()) < 0;
    });
    for (const auto& c : arc_codecs) { if ((c.CodecId & 0xffffff00U) != 0x040100) ++n_7z_codecs; }
  }
  for (size_t i = 0; i < n_format_libs; ++i) {
    if (arc_libs[i].SetCodecs) {
      auto compressinfo = new MyCompressCodecsInfo(arc_libs, arc_codecs, i);
      UInt32 n = 0;
      compressinfo->GetNumMethods(&n);
      if (n > 0) {
        compress_info.push_back(compressinfo);
        arc_libs[i].SetCodecs(compressinfo);
      }
      else {
        delete compressinfo;
      }
    }
  }
}

struct SfxModuleInfo {
  const wchar_t* module_name;
  unsigned descr_id;
  bool all_codecs;
  bool install_config;
};

const SfxModuleInfo c_known_sfx_modules[] = {
  { L"7z.sfx", MSG_SFX_DESCR_7Z, true, false },
  { L"7zCon.sfx", MSG_SFX_DESCR_7ZCON, true, false },
  { L"7zS.sfx", MSG_SFX_DESCR_7ZS, false, true },
  { L"7zSD.sfx", MSG_SFX_DESCR_7ZSD, false, true },
  { L"7zS2.sfx", MSG_SFX_DESCR_7ZS2, false, false },
  { L"7zS2con.sfx", MSG_SFX_DESCR_7ZS2CON, false, false },
};

const SfxModuleInfo* find(const wstring& path) {
  unsigned i = 0;
  for (; i < ARRAYSIZE(c_known_sfx_modules) && upcase(extract_file_name(path)) != upcase(c_known_sfx_modules[i].module_name); i++);
  if (i < ARRAYSIZE(c_known_sfx_modules))
    return c_known_sfx_modules + i;
  else
    return nullptr;
}

wstring SfxModule::description() const {
  const SfxModuleInfo* info = find(path);
  return info ? Far::get_msg(info->descr_id) : Far::get_msg(MSG_SFX_DESCR_UNKNOWN) + L" [" + extract_file_name(path) + L"]";
}

bool SfxModule::all_codecs() const {
  const SfxModuleInfo* info = find(path);
  return info ? info->all_codecs : true;
}

bool SfxModule::install_config() const {
  const SfxModuleInfo* info = find(path);
  return info ? info->install_config : true;
}

void ArcAPI::find_sfx_modules(const wstring& path) {
  FileEnum file_enum(path);
  wstring dir = extract_file_path(path);
  bool more;
  while (file_enum.next_nt(more) && more) {
    SfxModule sfx_module;
    sfx_module.path = add_trailing_slash(dir) + file_enum.data().cFileName;
    File file;
    if (!file.open_nt(sfx_module.path, FILE_READ_DATA, FILE_SHARE_READ, OPEN_EXISTING, 0))
      continue;
    Buffer<char> buffer(2);
    size_t sz;
    if (!file.read_nt(buffer.data(), buffer.size(), sz))
      continue;
    string sig(buffer.data(), sz);
    if (sig != "MZ")
      continue;
    sfx_modules.push_back(sfx_module);
  }
}

static bool ParseSignatures(const Byte *data, size_t size, vector<ByteVector> &signatures)
{
  signatures.clear();
  while (size > 0)
  {
    size_t len = *data++;
    size--;
    if (len > size)
      return false;

    ByteVector v(data, data+len);
    signatures.push_back(v);

    data += len;
    size -= len;
  }
  return true;
}

void ArcAPI::load() {
  auto dll_path = add_trailing_slash(Far::get_plugin_module_path());
  load_libs(dll_path + L"*.dll");
  find_sfx_modules(dll_path + L"*.sfx");
  if (arc_libs.empty() || sfx_modules.empty()) {
    wstring _7zip_path;
    Key _7zip_key;
    _7zip_key.open_nt(HKEY_CURRENT_USER, L"Software\\7-Zip", KEY_QUERY_VALUE, false) && _7zip_key.query_str_nt(_7zip_path, L"Path");
    if (_7zip_path.empty())
      _7zip_key.open_nt(HKEY_LOCAL_MACHINE, L"Software\\7-Zip", KEY_QUERY_VALUE, false) && _7zip_key.query_str_nt(_7zip_path, L"Path");
    if (!_7zip_path.empty()) {
      _7zip_path = add_trailing_slash(_7zip_path);
      if (arc_libs.empty()) {
        dll_path = _7zip_path;
        load_libs(_7zip_path + L"7z.dll");
      }
      if (!arc_libs.empty() && sfx_modules.empty())
        find_sfx_modules(_7zip_path + L"*.sfx");
    }
  }
  if (arc_libs.empty()) {
    wstring _7z_dll_path;
    IGNORE_ERRORS(_7z_dll_path = search_path(L"7z.dll"));
    if (!_7z_dll_path.empty()) {
      load_libs(_7z_dll_path);
      if (!arc_libs.empty()) {
        dll_path = add_trailing_slash(extract_file_path(_7z_dll_path));
        if (sfx_modules.empty())
          find_sfx_modules(dll_path + L"*.sfx");
      }
    }
  }

  n_base_format_libs = n_format_libs = arc_libs.size();
  load_libs(dll_path + L"Formats\\*.format");
  n_format_libs = arc_libs.size();
  load_codecs(dll_path + L"Codecs\\*.codec");

  for (unsigned i = 0; i < n_format_libs; i++) {
    const ArcLib& arc_lib = arc_libs[i];

    UInt32 num_formats;
    if (arc_lib.GetNumberOfFormats) {
      if (arc_lib.GetNumberOfFormats(&num_formats) != S_OK)
        num_formats = 0;
    }
    else
      num_formats = 1;

    for (UInt32 idx = 0; idx < num_formats; idx++) {
      ArcFormat format;

      if (arc_lib.get_bytes_prop(idx, NArchive::NHandlerPropID::kClassID, format.ClassID) != S_OK)
        continue;

      arc_lib.get_string_prop(idx, NArchive::NHandlerPropID::kName, format.name);
      if (arc_lib.get_bool_prop(idx, NArchive::NHandlerPropID::kUpdate, format.updatable) != S_OK)
        format.updatable = false;

      wstring extension_list_str;
      arc_lib.get_string_prop(idx, NArchive::NHandlerPropID::kExtension, extension_list_str);
      format.extension_list = split(extension_list_str, L' ');
      wstring add_extension_list_str;
      arc_lib.get_string_prop(idx, NArchive::NHandlerPropID::kAddExtension, add_extension_list_str);
      std::list<wstring> add_extension_list = split(add_extension_list_str, L' ');
      auto add_ext_iter = add_extension_list.cbegin();
      for (auto ext_iter = format.extension_list.begin(); ext_iter != format.extension_list.end(); ++ext_iter) {
        ext_iter->insert(0, 1, L'.');
        if (add_ext_iter != add_extension_list.cend()) {
          if (*add_ext_iter != L"*") {
            format.nested_ext_mapping[upcase(*ext_iter)] = *add_ext_iter;
          }
          ++add_ext_iter;
        }
      }

      format.lib_index = (int)i;
      format.FormatIndex = idx;

      format.NewInterface = arc_lib.get_uint_prop(idx, NArchive::NHandlerPropID::kFlags, format.Flags) == S_OK;
      if (!format.NewInterface) { // support for DLL version < 9.31:
        bool v = false;
        if (arc_lib.get_bool_prop(idx, NArchive::NHandlerPropID::kKeepName, v) != S_OK && v)
          format.Flags |= NArcInfoFlags::kKeepName;
        if (arc_lib.get_bool_prop(idx, NArchive::NHandlerPropID::kAltStreams, v) != S_OK && v)
          format.Flags |= NArcInfoFlags::kAltStreams;
        if (arc_lib.get_bool_prop(idx, NArchive::NHandlerPropID::kNtSecure, v) != S_OK && v)
          format.Flags |= NArcInfoFlags::kNtSecure;
      }

      ByteVector sig;
      arc_lib.get_bytes_prop(idx, NArchive::NHandlerPropID::kSignature, sig);
      if (!sig.empty())
        format.Signatures.push_back(sig);
      else {
        arc_lib.get_bytes_prop(idx, NArchive::NHandlerPropID::kMultiSignature, sig);
        ParseSignatures(sig.data(), sig.size(), format.Signatures);
      }

      if (arc_lib.get_uint_prop(idx, NArchive::NHandlerPropID::kSignatureOffset, format.SignatureOffset) != S_OK)
        format.SignatureOffset = 0;

      if (arc_lib.GetIsArc)
        arc_lib.GetIsArc(idx, &format.IsArc);

      ArcFormats::const_iterator existing_format = arc_formats.find(format.ClassID);
      if (existing_format == arc_formats.end() || arc_libs[existing_format->second.lib_index].version < arc_lib.version)
        arc_formats[format.ClassID] = format;
    }
  }
  // unload unused libraries
  set<unsigned> used_libs;
  for_each(arc_formats.begin(), arc_formats.end(), [&] (const pair<ArcType, ArcFormat>& arc_format) {
    used_libs.insert(arc_format.second.lib_index);
  });
  for (unsigned i = 0; i < n_format_libs; i++) {
    if (used_libs.count(i) == 0) {
      FreeLibrary(arc_libs[i].h_module);
      arc_libs[i].h_module = nullptr;
    }
  }
}

void ArcAPI::create_in_archive(const ArcType& arc_type, IInArchive** in_arc) {
  CHECK_COM(libs()[formats().at(arc_type).lib_index].CreateObject(reinterpret_cast<const GUID*>(arc_type.data()), &IID_IInArchive, reinterpret_cast<void**>(in_arc)));
}

void ArcAPI::create_out_archive(const ArcType& arc_type, IOutArchive** out_arc) {
  CHECK_COM(libs()[formats().at(arc_type).lib_index].CreateObject(reinterpret_cast<const GUID*>(arc_type.data()), &IID_IOutArchive, reinterpret_cast<void**>(out_arc)));
}

void ArcAPI::free() {
  if (arc_api) {
    delete arc_api;
    arc_api = nullptr;
  }
}


wstring Archive::get_default_name() const {
  wstring name = arc_name();
  wstring ext = extract_file_ext(name);
  name.erase(name.size() - ext.size(), ext.size());
  if (arc_chain.empty())
    return name;
  const ArcType& arc_type = arc_chain.back().type;
  auto& nested_ext_mapping = ArcAPI::formats().at(arc_type).nested_ext_mapping;
  auto nested_ext_iter = nested_ext_mapping.find(upcase(ext));
  if (nested_ext_iter == nested_ext_mapping.end())
    return name;
  const wstring& nested_ext = nested_ext_iter->second;
  ext = extract_file_ext(name);
  if (upcase(nested_ext) == upcase(ext))
    return name;
  name.replace(name.size() - ext.size(), ext.size(), nested_ext);
  return name;
}

wstring Archive::get_temp_file_name() const {
  GUID guid;
  CHECK_COM(CoCreateGuid(&guid));
  wchar_t guid_str[50];
  CHECK(StringFromGUID2(guid, guid_str, ARRAYSIZE(guid_str)));
  return add_trailing_slash(arc_dir()) + guid_str + L".tmp";
}


bool ArcFileInfo::operator<(const ArcFileInfo& file_info) const {
  if (parent == file_info.parent)
    if (is_dir == file_info.is_dir)
      return lstrcmpiW(name.c_str(), file_info.name.c_str()) < 0;
    else
      return is_dir;
  else
    return parent < file_info.parent;
}


void Archive::make_index() {
  num_indices = 0;
  CHECK_COM(in_arc->GetNumberOfItems(&num_indices));
  file_list.clear();
  file_list.reserve(num_indices);

  struct DirInfo {
    UInt32 index;
    UInt32 parent;
    wstring name;
    bool operator<(const DirInfo& dir_info) const {
      if (parent == dir_info.parent)
        return lstrcmpiW(name.c_str(), dir_info.name.c_str()) < 0;
      else
        return parent < dir_info.parent;
    }
  };
  typedef set<DirInfo> DirList;
  map<UInt32, unsigned> dir_index_map;
  DirList dir_list;

  DirInfo dir_info;
  UInt32 dir_index = 0;
  ArcFileInfo file_info;
  wstring path;
  PropVariant prop;
  for (UInt32 i = 0; i < num_indices; i++) {
    // is directory?
    file_info.is_dir = in_arc->GetProperty(i, kpidIsDir, prop.ref()) == S_OK && prop.is_bool() && prop.get_bool();

    // file name
    if (in_arc->GetProperty(i, kpidPath, prop.ref()) == S_OK && prop.is_str())
      path.assign(prop.get_str());
    else
      path.assign(get_default_name());
    size_t name_end_pos = path.size();
    while (name_end_pos && is_slash(path[name_end_pos - 1])) name_end_pos--;
    size_t name_pos = name_end_pos;
    while (name_pos && !is_slash(path[name_pos - 1])) name_pos--;
    file_info.name.assign(path.data() + name_pos, name_end_pos - name_pos);

    // split path into individual directories and put them into DirList
    dir_info.parent = c_root_index;
    stack<UInt32> dir_parents;
    size_t begin_pos = 0;
    while (begin_pos < name_pos) {
      dir_info.index = dir_index;
      size_t end_pos = begin_pos;
      while (end_pos < name_pos && !is_slash(path[end_pos])) end_pos++;
      if (end_pos != begin_pos) {
        dir_info.name=path.substr(begin_pos, end_pos - begin_pos);
        if(dir_info.name == L"..")
        {
          if(!dir_parents.empty())
          {
            dir_info.parent = dir_parents.top();
            dir_parents.pop();
          }
        }
        else if(dir_info.name != L".")
        {
          pair<DirList::iterator, bool> ins_pos = dir_list.insert(dir_info);
          if (ins_pos.second)
            dir_index++;
          dir_parents.push(dir_info.parent);
          dir_info.parent = ins_pos.first->index;
        }
      }
      begin_pos = end_pos + 1;
    }
    file_info.parent = dir_info.parent;

    if (file_info.is_dir) {
      dir_info.index = dir_index;
      dir_info.parent = file_info.parent;
      dir_info.name = file_info.name;
      pair<DirList::iterator, bool> ins_pos = dir_list.insert(dir_info);
      if (ins_pos.second) {
        dir_index++;
        dir_index_map[dir_info.index] = i;
      }
      else {
        if (dir_index_map.count(ins_pos.first->index))
          file_info.parent = c_dup_index;
        else
          dir_index_map[ins_pos.first->index] = i;
      }
    }

    file_list.push_back(file_info);
  }

  // add directories that not present in archive index
  file_list.reserve(file_list.size() + dir_list.size() - dir_index_map.size());
  dir_index = num_indices;
  for_each(dir_list.begin(), dir_list.end(), [&] (const DirInfo& dir_info) {
    if (dir_index_map.count(dir_info.index) == 0) {
      dir_index_map[dir_info.index] = dir_index;
      file_info.parent = dir_info.parent;
      file_info.name = dir_info.name;
      file_info.is_dir = true;
      dir_index++;
      file_list.push_back(file_info);
    }
  });

  // fix parent references
  for_each(file_list.begin(), file_list.end(), [&] (ArcFileInfo& file_info) {
    if (file_info.parent != c_root_index)
      file_info.parent = dir_index_map[file_info.parent];
  });

  // create search index
  file_list_index.clear();
  file_list_index.reserve(file_list.size());
  for (UInt32 i = 0; i < file_list.size(); i++) {
    file_list_index.push_back(i);
  }
  sort(file_list_index.begin(), file_list_index.end(), [&] (UInt32 left, UInt32 right) -> bool {
    return file_list[left] < file_list[right];
  });

  load_arc_attr();
}

UInt32 Archive::find_dir(const wstring& path) {
  if (file_list.empty())
    make_index();

  ArcFileInfo dir_info;
  dir_info.is_dir = true;
  dir_info.parent = c_root_index;
  size_t begin_pos = 0;
  while (begin_pos < path.size()) {
    size_t end_pos = begin_pos;
    while (end_pos < path.size() && !is_slash(path[end_pos])) end_pos++;
    if (end_pos != begin_pos) {
      dir_info.name.assign(path.data() + begin_pos, end_pos - begin_pos);
      FileIndexRange fi_range = equal_range(file_list_index.begin(), file_list_index.end(), -1, [&] (UInt32 left, UInt32 right) -> bool {
        const ArcFileInfo& fi_left = left == -1 ? dir_info : file_list[left];
        const ArcFileInfo& fi_right = right == -1 ? dir_info : file_list[right];
        return fi_left < fi_right;
      });
      if (fi_range.first == fi_range.second)
        FAIL(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND));
      dir_info.parent = *fi_range.first;
    }
    begin_pos = end_pos + 1;
  }
  return dir_info.parent;
}

FileIndexRange Archive::get_dir_list(UInt32 dir_index) {
  if (file_list.empty())
    make_index();

  ArcFileInfo file_info;
  file_info.parent = dir_index;
  FileIndexRange index_range = equal_range(file_list_index.begin(), file_list_index.end(), -1, [&] (UInt32 left, UInt32 right) -> bool {
    const ArcFileInfo& fi_left = left == -1 ? file_info : file_list[left];
    const ArcFileInfo& fi_right = right == -1 ? file_info : file_list[right];
    return fi_left.parent < fi_right.parent;
  });

  return index_range;
}

wstring Archive::get_path(UInt32 index) {
  if (file_list.empty())
    make_index();

  wstring file_path = file_list[index].name;
  UInt32 parent = file_list[index].parent;
  while (parent != c_root_index) {
    file_path.insert(0, 1, L'\\').insert(0, file_list[parent].name);
    parent = file_list[parent].parent;
  }
  return file_path;
}

FindData Archive::get_file_info(UInt32 index) {
  if (file_list.empty())
    make_index();

  FindData file_info;
  memzero(file_info);
  wcscpy(file_info.cFileName, file_list[index].name.c_str());
  file_info.dwFileAttributes = get_attr(index);
  file_info.set_size(get_size(index));
  file_info.ftCreationTime = get_ctime(index);
  file_info.ftLastWriteTime = get_mtime(index);
  file_info.ftLastAccessTime = get_atime(index);
  return file_info;
}

bool Archive::get_main_file(UInt32& index) const {
  PropVariant prop;
  if (in_arc->GetArchiveProperty(kpidMainSubfile, prop.ref()) != S_OK || prop.vt != VT_UI4)
    return false;
  index = prop.ulVal;
  return true;
}

DWORD Archive::get_attr(UInt32 index) const {
  PropVariant prop;
  DWORD attr = 0;

  if (index >= num_indices)
    return FILE_ATTRIBUTE_DIRECTORY;
  
  if (in_arc->GetProperty(index, kpidAttrib, prop.ref()) == S_OK && prop.is_uint())
    attr = static_cast<DWORD>(prop.get_uint());

  if (file_list[index].is_dir)
    attr |= FILE_ATTRIBUTE_DIRECTORY;

  return attr;
}

unsigned __int64 Archive::get_size(UInt32 index) const {
  PropVariant prop;
  if (index >= num_indices)
    return 0;
  else if (!file_list[index].is_dir && in_arc->GetProperty(index, kpidSize, prop.ref()) == S_OK && prop.is_uint())
    return prop.get_uint();
  else
    return 0;
}

unsigned __int64 Archive::get_psize(UInt32 index) const {
  PropVariant prop;
  if (index >= num_indices)
    return 0;
  else if (!file_list[index].is_dir && in_arc->GetProperty(index, kpidPackSize, prop.ref()) == S_OK && prop.is_uint())
    return prop.get_uint();
  else
    return 0;
}

FILETIME Archive::get_ctime(UInt32 index) const {
  PropVariant prop;
  if (index >= num_indices)
    return arc_info.ftCreationTime;
  else if (in_arc->GetProperty(index, kpidCTime, prop.ref()) == S_OK && prop.is_filetime())
    return prop.get_filetime();
  else
    return arc_info.ftCreationTime;
}

FILETIME Archive::get_mtime(UInt32 index) const {
  PropVariant prop;
  if (index >= num_indices)
    return arc_info.ftLastWriteTime;
  else if (in_arc->GetProperty(index, kpidMTime, prop.ref()) == S_OK && prop.is_filetime())
    return prop.get_filetime();
  else
    return arc_info.ftLastWriteTime;
}

FILETIME Archive::get_atime(UInt32 index) const {
  PropVariant prop;
  if (index >= num_indices)
    return arc_info.ftLastAccessTime;
  else if (in_arc->GetProperty(index, kpidATime, prop.ref()) == S_OK && prop.is_filetime())
    return prop.get_filetime();
  else
    return arc_info.ftLastAccessTime;
}

unsigned Archive::get_crc(UInt32 index) const {
  PropVariant prop;
  if (index >= num_indices)
    return 0;
  else if (in_arc->GetProperty(index, kpidCRC, prop.ref()) == S_OK && prop.is_uint())
    return static_cast<DWORD>(prop.get_uint());
  else
    return 0;
}

bool Archive::get_anti(UInt32 index) const {
  PropVariant prop;
  if (index >= num_indices)
    return false;
  else if (in_arc->GetProperty(index, kpidIsAnti, prop.ref()) == S_OK && prop.is_bool())
    return prop.get_bool();
  else
    return false;
}
