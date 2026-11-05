// OS/donor ABI selection is independent of GPU family and board wiring.
// The installer enforces product-version minima; runtime patches still verify bytes.
#ifndef KERN_XELP_OS_HPP
#define KERN_XELP_OS_HPP
namespace XELP {
enum class OS { Unknown, Donor, BigSur, Monterey, Ventura, Sonoma, Sequoia, Tahoe };
inline bool hostOS(OS os) {
    return os>=OS::BigSur && os<=OS::Tahoe;
}
inline OS kernelOS(const char *version) {
    // The original DP1 execution environment is a separate exact identity.
    // Supported host families below never depend on XNU revision/date text.
    static const char donor[]="Darwin Kernel Version 20.0.0: Sat Jun 13 17:58:16 PDT 2020; root:xnu-7090.0.0.111.5~1/RELEASE_X86_64";
    if (!version) return OS::Unknown;
    const char *a=version,*b=donor;
    while (*a && *a==*b) {++a;++b;}
    if (*a==*b) return OS::Donor;
    // Servicing updates must not silently bypass ABI setup because the build
    // date/XNU revision changed. Darwin 24.6 cannot distinguish macOS 15.7
    // from 15.8: the package enforces 15.8.x. Darwin 25.6+ covers 26.7+.
    const char *p=version,*prefix="Darwin Kernel Version ";
    while (*prefix && *p==*prefix) {++p;++prefix;}
    if (*prefix) return OS::Unknown;
    unsigned numbers[3] {};
    for (unsigned i=0;i<3;++i) {
        if (*p<'0' || *p>'9') return OS::Unknown;
        do {
            numbers[i]=numbers[i]*10+static_cast<unsigned>(*p++-'0');
            if (numbers[i]>999) return OS::Unknown;
        } while (*p>='0' && *p<='9');
        if (*p++!=(i==2 ? ':' : '.')) return OS::Unknown;
    }
    if (*p++!=' ') return OS::Unknown;
    const char *end=p; while (*end) ++end;
    const char suffix[]="/RELEASE_X86_64";
    if (end-p<static_cast<long>(sizeof(suffix)-1)) return OS::Unknown;
    const char *tail=end-(sizeof(suffix)-1);
    for (unsigned i=0;i<sizeof(suffix);++i)
        if (tail[i]!=suffix[i]) return OS::Unknown;
    // Product minima are enforced by the package. These final Darwin lines
    // include servicing releases whose metadata and patch number can change.
    if (numbers[0]>=20 && numbers[0]<=24 && numbers[1]==6)
        return static_cast<OS>(unsigned(OS::BigSur)+numbers[0]-20);
    if (numbers[0]==25 && numbers[1]>=6) return OS::Tahoe;
    return OS::Unknown;
}
}
#endif
