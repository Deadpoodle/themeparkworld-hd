// How often the desktop really changes, from DXGI desktop duplication (what DWM
// composes and the monitor shows). GDI screen grabs can be stale for DirectX
// windows; this can't be.
//   dupfps.exe <seconds> [output index]   prints updates per second
// Build: g++ -O2 -o build/dupfps.exe tools/dupfps.cpp -ld3d11 -ldxgi
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    double secs = argc > 1 ? atof(argv[1]) : 5;
    UINT outputIndex = argc > 2 ? atoi(argv[2]) : 0;
    const char* saveDir = argc > 3 ? argv[3] : nullptr;  // save every 50 ms, 1/4 size, up to 48 BMPs
    int saved = 0;
    LARGE_INTEGER lastSave = {};
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) {
        puts("no D3D11 device");
        return 1;
    }
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    IDXGIOutput1* output1 = nullptr;
    IDXGIOutputDuplication* dup = nullptr;
    dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi);
    dxgi->GetAdapter(&adapter);
    if (FAILED(adapter->EnumOutputs(outputIndex, &output))) {
        puts("no such output");
        return 1;
    }
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    HRESULT hr = output1->DuplicateOutput(dev, &dup);
    if (FAILED(hr)) {
        printf("DuplicateOutput failed %#lx\n", (unsigned long)hr);
        return 1;
    }
    LARGE_INTEGER f, t0, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    int frames = 0, accumulated = 0, changed = 0;
    unsigned long long lastHash = 0;
    ID3D11Texture2D* staging = nullptr;
    for (;;) {
        QueryPerformanceCounter(&t);
        double el = double(t.QuadPart - t0.QuadPart) / f.QuadPart;
        if (el >= secs) {
            printf("desktop updates: %d in %.1f s = %.1f/s (accumulated presents %d); content changed %d = %.1f/s\n",
                   frames, el, frames / el, accumulated, changed, changed / el);
            return 0;
        }
        DXGI_OUTDUPL_FRAME_INFO info;
        IDXGIResource* res = nullptr;
        hr = dup->AcquireNextFrame(100, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (FAILED(hr)) {
            printf("AcquireNextFrame failed %#lx\n", (unsigned long)hr);
            return 1;
        }
        if (info.LastPresentTime.QuadPart) {  // a present (not just the pointer)
            ++frames;
            accumulated += info.AccumulatedFrames;
            // Did the picture change? Hash a sparse grid of pixels.
            ID3D11Texture2D* tex = nullptr;
            res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
            D3D11_TEXTURE2D_DESC d;
            tex->GetDesc(&d);
            if (!staging) {
                d.Usage = D3D11_USAGE_STAGING;
                d.BindFlags = 0;
                d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                d.MiscFlags = 0;
                dev->CreateTexture2D(&d, nullptr, &staging);
            }
            ctx->CopyResource(staging, tex);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
                unsigned long long h = 1469598103934665603ull;
                for (UINT y = 8; y < d.Height; y += 16)
                    for (UINT x = 8; x < d.Width; x += 16) {
                        h ^= ((const unsigned*)((const char*)m.pData + y * m.RowPitch))[x];
                        h *= 1099511628211ull;
                    }
                QueryPerformanceCounter(&t);
                if (saveDir && saved < 48 && (t.QuadPart - lastSave.QuadPart) * 1000 / f.QuadPart >= 50) {
                    lastSave = t;
                    UINT w = d.Width / 4, hgt = d.Height / 4;
                    char path[MAX_PATH];
                    snprintf(path, sizeof path, "%s/f%02d.bmp", saveDir, saved++);
                    if (FILE* fp = fopen(path, "wb")) {
                        BITMAPFILEHEADER fh = {0x4d42, (DWORD)(54 + w * hgt * 4), 0, 0, 54};
                        BITMAPINFOHEADER ih = {40, (LONG)w, -(LONG)hgt, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
                        fwrite(&fh, sizeof fh, 1, fp);
                        fwrite(&ih, sizeof ih, 1, fp);
                        for (UINT y = 0; y < hgt; ++y)
                            for (UINT x = 0; x < w; ++x)
                                fwrite((const char*)m.pData + (y * 4) * m.RowPitch + (x * 4) * 4, 4, 1, fp);
                        fclose(fp);
                    }
                }
                ctx->Unmap(staging, 0);
                if (h != lastHash) ++changed;
                lastHash = h;
            }
            tex->Release();
        }
        res->Release();
        dup->ReleaseFrame();
    }
}
