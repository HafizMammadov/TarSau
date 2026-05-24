/*
 * tarsau.c - Arşivleme programı (sıkıştırmasız)
 * Kullanım:
 * tarsau -b dosya1 dosya2 ... -o arsiv.sau   (arşiv oluştur)
 * tarsau -a arsiv.sau [dizin]                (arşivden çıkart)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

#define MAX_FILES        32
#define MAX_PATH         4096
#define MAX_TOTAL_BYTES  (200LL * 1024LL * 1024LL)   /* 200 MB */
#define ORG_SIZE_FIELD   10                            /* ilk 10 bayt: org bölüm boyutu */
#define DEFAULT_ARCHIVE  "a.sau"
#define SAU_EXT          ".sau"

/* ------------------------------------------------------------------ */
/* Yardımcı: izin bitlerini "rwxrwxrwx" dizisine çevir               */
/* ------------------------------------------------------------------ */
static void mode_to_str(mode_t mode, char *buf)
{
    buf[0] = (mode & S_IRUSR) ? 'r' : '-';
    buf[1] = (mode & S_IWUSR) ? 'w' : '-';
    buf[2] = (mode & S_IXUSR) ? 'x' : '-';
    buf[3] = (mode & S_IRGRP) ? 'r' : '-';
    buf[4] = (mode & S_IWGRP) ? 'w' : '-';
    buf[5] = (mode & S_IXGRP) ? 'x' : '-';
    buf[6] = (mode & S_IROTH) ? 'r' : '-';
    buf[7] = (mode & S_IWOTH) ? 'w' : '-';
    buf[8] = (mode & S_IXOTH) ? 'x' : '-';
    buf[9] = '\0';
}

/* ------------------------------------------------------------------ */
/* Yardımcı: "rwxrwxrwx" dizisini mode_t değerine çevir              */
/* ------------------------------------------------------------------ */
static mode_t str_to_mode(const char *buf)
{
    mode_t m = 0;
    if (strlen(buf) < 9) return 0644;
    if (buf[0] == 'r') m |= S_IRUSR;
    if (buf[1] == 'w') m |= S_IWUSR;
    if (buf[2] == 'x') m |= S_IXUSR;
    if (buf[3] == 'r') m |= S_IRGRP;
    if (buf[4] == 'w') m |= S_IWGRP;
    if (buf[5] == 'x') m |= S_IXGRP;
    if (buf[6] == 'r') m |= S_IROTH;
    if (buf[7] == 'w') m |= S_IWOTH;
    if (buf[8] == 'x') m |= S_IXOTH;
    return m;
}

/* ------------------------------------------------------------------ */
/* Yardımcı: dosyanın metin dosyası olup olmadığını kontrol et        */
/* Standart ASCII kısıtlamalarına tam uyum denetimi                  */
/* ------------------------------------------------------------------ */
static int is_text_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    int c;
    while ((c = fgetc(fp)) != EOF) {
        /* Karakter 0-127 aralığında olmalıdır (Standart ASCII) */
        if (c < 0 || c > 127) {
            fclose(fp);
            return 0;
        }
        /* Metin dosyalarında izin verilen kontrol karakterleri hariç tutulur:
           Tab (\t), Satır Sonu (\n), Satır Başı (\r) */
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Yardımcı: yalnızca dosya adını (yolu değil) döndür                 */
/* ------------------------------------------------------------------ */
static const char *basename_of(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/* ------------------------------------------------------------------ */
/* Yardımcı: dizini (ve ara dizinleri) oluştur                        */
/* ------------------------------------------------------------------ */
static int mkdir_p(const char *path)
{
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return 0;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                perror(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        perror(tmp);
        return -1;
    }
    return 0;
}

/* ================================================================== */
/* -b işlemi: arşiv oluştur                                           */
/* ================================================================== */
static int do_bundle(int file_count, char **files, const char *archive)
{
    struct stat st;
    long long total_size = 0;
    char perm_str[10];

    /* --- Ön kontroller --- */
    if (file_count == 0) {
        fprintf(stderr, "Hata: Arşivlenecek dosya belirtilmedi.\n");
        return 1;
    }
    if (file_count > MAX_FILES) {
        fprintf(stderr, "Hata: En fazla %d dosya arşivlenebilir.\n", MAX_FILES);
        return 1;
    }

    /* Her dosyayı kontrol et */
    for (int i = 0; i < file_count; i++) {
        if (stat(files[i], &st) != 0) {
            fprintf(stderr, "%s dosyasına erişilemiyor: %s\n",
                    files[i], strerror(errno));
            return 1;
        }
        if (!S_ISREG(st.st_mode)) {
            fprintf(stderr, "%s giriş dosyasının formatı uyumsuzdur!\n",
                    basename_of(files[i]));
            return 1;
        }
        if (!is_text_file(files[i])) {
            fprintf(stderr, "%s giriş dosyasının formatı uyumsuzdur!\n",
                    basename_of(files[i]));
            return 1;
        }
        total_size += st.st_size;
        if (total_size > MAX_TOTAL_BYTES) {
            fprintf(stderr, "Hata: Giriş dosyalarının toplam boyutu 200 MB'ı geçemez.\n");
            return 1;
        }
    }

    /* --- Organizasyon (içerik) bölümünü string olarak oluştur --- */
    char org_buf[MAX_FILES * (MAX_PATH + 32)];
    int org_pos = 0;
    org_buf[0] = '\0';

    for (int i = 0; i < file_count; i++) {
        stat(files[i], &st);
        mode_to_str(st.st_mode & 0777, perm_str);
        int written = snprintf(org_buf + org_pos,
                               sizeof(org_buf) - org_pos,
                               "|%s,%s,%lld",
                               basename_of(files[i]),
                               perm_str,
                               (long long)st.st_size);
        if (written < 0 || (size_t)written >= sizeof(org_buf) - org_pos) {
            fprintf(stderr, "Hata: Organizasyon bölümü oluşturulamadı.\n");
            return 1;
        }
        org_pos += written;
    }
    /* Son kapanış pipe */
    if (org_pos + 2 < (int)sizeof(org_buf)) {
        org_buf[org_pos++] = '|';
        org_buf[org_pos]   = '\0';
    }

    int org_body_len = org_pos;

    /* --- Arşiv dosyasını yaz --- */
    FILE *out = fopen(archive, "wb");
    if (!out) {
        fprintf(stderr, "Arşiv dosyası oluşturulamadı: %s\n", strerror(errno));
        return 1;
    }

    /* İlk 10 bayt: organizasyon bölümü boyutu (Sıfır dolgulu sabit 10 karakter) */
    char size_field[16];
    snprintf(size_field, sizeof(size_field), "%010ld", (long)org_body_len);
    if (fwrite(size_field, 1, ORG_SIZE_FIELD, out) != ORG_SIZE_FIELD) {
        fprintf(stderr, "Yazma hatası: %s\n", strerror(errno));
        fclose(out);
        return 1;
    }

    /* Organizasyon bölümünü yaz */
    if (fwrite(org_buf, 1, org_body_len, out) != (size_t)org_body_len) {
        fprintf(stderr, "Yazma hatası: %s\n", strerror(errno));
        fclose(out);
        return 1;
    }

    /* Dosya içeriklerini art arda yaz */
    for (int i = 0; i < file_count; i++) {
        FILE *in = fopen(files[i], "rb");
        if (!in) {
            fprintf(stderr, "%s açılamadı: %s\n", files[i], strerror(errno));
            fclose(out);
            return 1;
        }
        char copy_buf[65536];
        size_t n;
        while ((n = fread(copy_buf, 1, sizeof(copy_buf), in)) > 0) {
            if (fwrite(copy_buf, 1, n, out) != n) {
                fprintf(stderr, "Yazma hatası: %s\n", strerror(errno));
                fclose(in);
                fclose(out);
                return 1;
            }
        }
        fclose(in);
    }

    fclose(out);
    printf("Dosyalar birleştirildi.\n");
    return 0;
}

/* ================================================================== */
/* -a işlemi: arşivden çıkart                                         */
/* ================================================================== */
static int do_extract(const char *archive, const char *dest_dir)
{
    const char *ext = strrchr(archive, '.');
    if (!ext || strcmp(ext, SAU_EXT) != 0) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        return 1;
    }

    FILE *in = fopen(archive, "rb");
    if (!in) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        return 1;
    }

    /* --- İlk 10 baytı oku: organizasyon bölümü boyutu --- */
    char size_field[ORG_SIZE_FIELD + 1];
    if (fread(size_field, 1, ORG_SIZE_FIELD, in) != ORG_SIZE_FIELD) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(in);
        return 1;
    }
    size_field[ORG_SIZE_FIELD] = '\0';

    char *endptr;
    long org_size = strtol(size_field, &endptr, 10);
    if (org_size <= 0 || org_size > 10 * 1024 * 1024) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(in);
        return 1;
    }

    /* --- Organizasyon bölümünü oku --- */
    char *org_buf = (char *)malloc(org_size + 1);
    if (!org_buf) {
        fprintf(stderr, "Bellek yetersiz.\n");
        fclose(in);
        return 1;
    }
    if (fread(org_buf, 1, org_size, in) != (size_t)org_size) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        free(org_buf);
        fclose(in);
        return 1;
    }
    org_buf[org_size] = '\0';

    /* --- Organizasyon bölümünü ayrıştır --- */
    char  names[MAX_FILES][MAX_PATH];
    char  perms[MAX_FILES][12];
    long long sizes[MAX_FILES];
    int   nfiles = 0;

    char *p = org_buf;
    while (*p == '|' && nfiles < MAX_FILES) {
        p++; 
        if (*p == '\0') break; 

        char *comma1 = strchr(p, ',');
        if (!comma1) {
            fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
            free(org_buf);
            fclose(in);
            return 1;
        }
        int name_len = (int)(comma1 - p);
        if (name_len <= 0 || name_len >= MAX_PATH) {
            fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
            free(org_buf);
            fclose(in);
            return 1;
        }
        strncpy(names[nfiles], p, name_len);
        names[nfiles][name_len] = '\0';
        p = comma1 + 1;

        char *comma2 = strchr(p, ',');
        if (!comma2) {
            fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
            free(org_buf);
            fclose(in);
            return 1;
        }
        int perm_len = (int)(comma2 - p);
        if (perm_len <= 0 || perm_len > 11) {
            fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
            free(org_buf);
            fclose(in);
            return 1;
        }
        strncpy(perms[nfiles], p, perm_len);
        perms[nfiles][perm_len] = '\0';
        p = comma2 + 1;

        char *pipe_end = strchr(p, '|');
        if (!pipe_end) {
            sizes[nfiles] = atoll(p);
            p += strlen(p); 
        } else {
            int sz_len = (int)(pipe_end - p);
            char sz_buf[32] = {0};
            if (sz_len > 0 && sz_len < 20) {
                strncpy(sz_buf, p, sz_len);
                sizes[nfiles] = atoll(sz_buf);
            } else {
                fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
                free(org_buf);
                fclose(in);
                return 1;
            }
            p = pipe_end; 
        }

        nfiles++;
    }
    free(org_buf);

    if (nfiles == 0) {
        fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(in);
        return 1;
    }

    /* --- Hedef dizini hazırla --- */
    char out_dir[MAX_PATH] = ".";
    if (dest_dir && dest_dir[0] != '\0') {
        strncpy(out_dir, dest_dir, MAX_PATH - 1);
        out_dir[MAX_PATH - 1] = '\0';
        struct stat dstat;
        if (stat(out_dir, &dstat) != 0) {
            if (mkdir_p(out_dir) != 0) {
                fclose(in);
                return 1;
            }
        } else if (!S_ISDIR(dstat.st_mode)) {
            fprintf(stderr, "%s bir dizin değil.\n", out_dir);
            fclose(in);
            return 1;
        }
    }

    /* --- Dosyaları çıkart --- */
    char copy_buf[65536];
    for (int i = 0; i < nfiles; i++) {
        char out_path[MAX_PATH * 2];
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, names[i]);

        FILE *out = fopen(out_path, "wb");
        if (!out) {
            fprintf(stderr, "%s oluşturulamadı: %s\n", out_path, strerror(errno));
            fclose(in);
            return 1;
        }

        long long remaining = sizes[i];
        while (remaining > 0) {
            size_t to_read = (remaining > (long long)sizeof(copy_buf))
                             ? sizeof(copy_buf) : (size_t)remaining;
            size_t n = fread(copy_buf, 1, to_read, in);
            if (n == 0) {
                fprintf(stderr, "Arşiv dosyası uygunsuz veya bozuk! (beklenenden az veri)\n");
                fclose(out);
                fclose(in);
                return 1;
            }
            if (fwrite(copy_buf, 1, n, out) != n) {
                fprintf(stderr, "%s yazma hatası: %s\n", out_path, strerror(errno));
                fclose(out);
                fclose(in);
                return 1;
            }
            remaining -= (long long)n;
        }
        fclose(out);

        /* İzinleri geri yükle */
        mode_t mode = str_to_mode(perms[i]);
        chmod(out_path, mode);
    }

    fclose(in);

    if (dest_dir && dest_dir[0] != '\0') {
        printf("%s dizininde ", out_dir);
    } else {
        printf("Geçerli dizinde ");
    }

    for (int i = 0; i < nfiles; i++) {
        printf("%s", names[i]);
        if (i < nfiles - 1) printf(", ");
    }
    printf(" dosyaları açıldı.\n");
    return 0;
}

/* ================================================================== */
/* Ana program                                                         */
/* ================================================================== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
                "Kullanım:\n"
                "  %s -b dosya1 [dosya2 ...] [-o arsiv.sau]\n"
                "  %s -a arsiv.sau [dizin]\n",
                argv[0], argv[0]);
        return 1;
    }

    /* --- -b modu --- */
    if (strcmp(argv[1], "-b") == 0) {
        char *input_files[MAX_FILES];
        int   file_count   = 0;
        char *output_name  = DEFAULT_ARCHIVE;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Hata: -o parametresinden sonra dosya adı belirtilmedi.\n");
                    return 1;
                }
                output_name = argv[++i];
            } else {
                if (file_count >= MAX_FILES) {
                    fprintf(stderr, "Hata: En fazla %d dosya arşivlenebilir.\n", MAX_FILES);
                    return 1;
                }
                input_files[file_count++] = argv[i];
            }
        }
        return do_bundle(file_count, input_files, output_name);
    }

    /* --- -a modu --- */
    else if (strcmp(argv[1], "-a") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Hata: -a parametresinden sonra arşiv dosyası adı belirtilmedi.\n");
            return 1;
        }
        if (argc > 4) {
            fprintf(stderr, "Hata: -a parametresinden sonra en fazla 2 parametre alınabilir.\n");
            return 1;
        }
        const char *archive  = argv[2];
        const char *dest_dir = (argc == 4) ? argv[3] : "";
        return do_extract(archive, dest_dir);
    }

    else {
        fprintf(stderr, "Hata: Geçersiz parametre '%s'. Kullanım: -b veya -a\n", argv[1]);
        return 1;
    }
}
