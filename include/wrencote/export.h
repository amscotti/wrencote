#ifndef WRENCOTE_EXPORT_H
#define WRENCOTE_EXPORT_H

#if defined(_WIN32) && defined(WRENCOTE_SHARED)
#if defined(WC_BUILDING_LIBRARY)
#define WC_API __declspec(dllexport)
#else
#define WC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define WC_API __attribute__((visibility("default")))
#else
#define WC_API
#endif

#endif
