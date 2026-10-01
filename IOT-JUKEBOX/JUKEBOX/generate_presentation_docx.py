import os
import docx
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_ALIGN_VERTICAL
from docx.oxml import OxmlElement, parse_xml
from docx.oxml.ns import nsdecls, qn

def set_cell_background(cell, hex_color):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = parse_xml(f'<w:shd {nsdecls("w")} w:fill="{hex_color}"/>')
    tcPr.append(shd)

def set_cell_margins(cell, top=100, bottom=100, left=150, right=150):
    tcPr = cell._tc.get_or_add_tcPr()
    tcMar = OxmlElement('w:tcMar')
    for m, val in [('top', top), ('bottom', bottom), ('left', left), ('right', right)]:
        node = OxmlElement(f'w:{m}')
        node.set(qn('w:w'), str(val))
        node.set(qn('w:type'), 'dxa')
        tcMar.append(node)
    tcPr.append(tcMar)

def create_presentation_doc():
    doc = Document()

    # Set standard margins (1 inch)
    sections = doc.sections
    for section in sections:
        section.top_margin = Inches(1)
        section.bottom_margin = Inches(1)
        section.left_margin = Inches(1)
        section.right_margin = Inches(1)

    # Styles & Colors
    # Primary: Deep Navy/Slate (#1E3A8A)
    # Secondary: Teal/Cyan (#0D9488)
    # Accent: Minecraft Gold (#D97706)
    # Dark Text: #1F2937

    # ==========================================
    # COVER / HEADER TITLE
    # ==========================================
    title_p = doc.add_paragraph()
    title_p.paragraph_format.space_before = Pt(20)
    title_p.paragraph_format.space_after = Pt(6)
    title_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run_title = title_p.add_run("MATERI PRESENTASI PROYEK AKHIR / UJIAN PRAKTIK SMK")
    run_title.font.name = "Arial"
    run_title.font.size = Pt(13)
    run_title.font.bold = True
    run_title.font.color.rgb = RGBColor(13, 148, 136) # Teal

    h1_p = doc.add_paragraph()
    h1_p.paragraph_format.space_before = Pt(0)
    h1_p.paragraph_format.space_after = Pt(12)
    h1_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run_h1 = h1_p.add_run("SMART MINECRAFT JUKEBOX ESP32")
    run_h1.font.name = "Arial Black"
    run_h1.font.size = Pt(24)
    run_h1.font.color.rgb = RGBColor(30, 58, 138) # Deep Navy

    sub_p = doc.add_paragraph()
    sub_p.paragraph_format.space_before = Pt(0)
    sub_p.paragraph_format.space_after = Pt(24)
    sub_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run_sub = sub_p.add_run("Sistem Pemutar Musik Otomatis Berbasis Sensor Warna TCS3000,\nPenyimpanan Flash Internal LittleFS, dan Transmisi Bluetooth Audio A2DP")
    run_sub.font.name = "Arial"
    run_sub.font.size = Pt(11)
    run_sub.font.italic = True
    run_sub.font.color.rgb = RGBColor(75, 85, 99)

    # Info Box Table
    info_table = doc.add_table(rows=2, cols=2)
    info_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    cell_data = [
        ("Target Audiens:", "Siswa SMK (Jurusan RPL / TKJ / TEI / Mekatronika)"),
        ("Tingkat Materi:", "Pemula - Menengah (Bahasa Sederhana, Analogi Praktis & Visual)")
    ]
    for row_idx, (k, v) in enumerate(cell_data):
        cell_k = info_table.cell(row_idx, 0)
        cell_v = info_table.cell(row_idx, 1)
        cell_k.text = k
        cell_v.text = v
        set_cell_background(cell_k, "F3F4F6")
        set_cell_background(cell_v, "F9FAFB")
        cell_k.paragraphs[0].runs[0].font.bold = True
        cell_k.paragraphs[0].runs[0].font.size = Pt(10)
        cell_v.paragraphs[0].runs[0].font.size = Pt(10)
    
    doc.add_paragraph().paragraph_format.space_after = Pt(18)

    # Helper function for sections
    def add_section_header(title, slide_num=None):
        h = doc.add_paragraph()
        h.paragraph_format.space_before = Pt(18)
        h.paragraph_format.space_after = Pt(8)
        prefix = f"SLIDE {slide_num}: " if slide_num else ""
        run = h.add_run(f"{prefix}{title.upper()}")
        run.font.name = "Arial"
        run.font.size = Pt(14)
        run.font.bold = True
        run.font.color.rgb = RGBColor(30, 58, 138)
        
        # Horizontal line
        p_line = doc.add_paragraph()
        p_line.paragraph_format.space_before = Pt(0)
        p_line.paragraph_format.space_after = Pt(10)
        run_line = p_line.add_run("―" * 55)
        run_line.font.color.rgb = RGBColor(209, 213, 219)

    # ==========================================
    # SLIDE 1: PENGENALAN & LATAR BELAKANG
    # ==========================================
    add_section_header("Latar Belakang & Ide Kreatif Proyek", 1)
    
    p = doc.add_paragraph()
    p.add_run("Apa itu Minecraft Jukebox di Dunia Nyata?\n").bold = True
    p.add_run(
        "Bagi para pemain game Minecraft, Jukebox adalah balok kayu unik tempat pemain memasukkan piringan lagu (Music Disc). "
        "Tiap kaset memiliki warna berbeda dan memutar lagu yang berbeda pula. "
        "Di proyek ini, kita membawa konsep game tersebut menjadi alat nyata (Internet of Things / Embedded System)!"
    )
    
    p2 = doc.add_paragraph()
    p2.add_run("Kenapa Proyek Ini Keren & Relevan untuk SMK?\n").bold = True
    p2.add_run(
        "1. Bukan sekadar teori mikrokontroler: Siswa belajar memadukan 4 pilar teknologi sekaligus: sensor optik, filesystem chip, pemrosesan audio digital, dan komunikasi nirkabel Bluetooth.\n"
        "2. Hemat Biaya & Ringkas: Tidak memerlukan modul MicroSD tambahan yang sering macet atau kendor di lapangan. Seluruh lagu disimpan langsung di dalam chip ESP32.\n"
        "3. Interaktif & Menyenangkan: Menghubungkan dunia game dengan coding dan elektronika fisik."
    )

    # ==========================================
    # SLIDE 2: TUJUAN PROYEK
    # ==========================================
    add_section_header("Tujuan & Manfaat Pembelajaran", 2)
    
    table_tujuan = doc.add_table(rows=1, cols=2)
    hdr_cells = table_tujuan.rows[0].cells
    hdr_cells[0].text = "Tujuan Pembelajaran"
    hdr_cells[1].text = "Skill Nyata yang Didapat Siswa SMK"
    set_cell_background(hdr_cells[0], "1E3A8A")
    set_cell_background(hdr_cells[1], "1E3A8A")
    hdr_cells[0].paragraphs[0].runs[0].font.color.rgb = RGBColor(255, 255, 255)
    hdr_cells[1].paragraphs[0].runs[0].font.color.rgb = RGBColor(255, 255, 255)
    hdr_cells[0].paragraphs[0].runs[0].font.bold = True
    hdr_cells[1].paragraphs[0].runs[0].font.bold = True

    tujuan_rows = [
        ("Mempelajari Karakteristik Sensor Optik", "Memahami bagaimana fotodioda membaca pantulan warna RGB dan membedakan benda berdasarkan frekuensi gelombang."),
        ("Memahami Filesystem LittleFS pada ESP32", "Belajar partisi memori flash chip tanpa media eksternal (MicroSD card)."),
        ("Menguasai Audio Streaming Nirkabel (A2DP)", "Memahami protokol Bluetooth Audio Classic dan bagaimana data PCM dialirkan ke speaker."),
        ("Multitasking dengan FreeRTOS Dual Core", "Mengetahui bagaimana membagi tugas CPU: Core 0 untuk transmisi radio, Core 1 untuk sensor & MP3 decoder."),
        ("Membuat Web Dashboard Monitoring", "Membuat antarmuka lokal di browser untuk memantau status alat secara live via USB Serial.")
    ]
    for k, v in tujuan_rows:
        row = table_tujuan.add_row()
        c0, c1 = row.cells
        c0.text = k
        c1.text = v
        c0.paragraphs[0].runs[0].font.bold = True
        c0.paragraphs[0].runs[0].font.size = Pt(9.5)
        c1.paragraphs[0].runs[0].font.size = Pt(9.5)
        set_cell_background(c0, "F3F4F6")
        set_cell_background(c1, "FFFFFF")

    # ==========================================
    # SLIDE 3: KOMPONEN & FUNGSI (ANALOGI MUDAH)
    # ==========================================
    add_section_header("Alat, Bahan & Analogi Komponen", 3)
    
    komponen_list = [
        ("ESP32 Dev Module (Otak)", "Komputer mini 32-bit dengan 2 inti CPU (Dual Core 240MHz). Berfungsi sebagai pengendali utama, pembaca sensor, pengurai (decoder) file lagu MP3, dan pemancar Bluetooth."),
        ("Sensor TCS3000 / TCS3200 (Mata)", "Mata digital yang memiliki 4 lampu LED putih dan barisan fotodioda dengan filter Merah, Hijau, Biru, dan Bening. Sensor ini mengubah pantulan warna menjadi frekuensi pulsa gelombang."),
        ("Memori Flash Internal LittleFS (Gudang Lagu)", "Ruang penyimpanan bawaan di dalam chip ESP32 (sebesar 2.8 MegaByte). Menampung file audio .mp3 seperti kaset mini permanen."),
        ("Speaker Bluetooth YMX-868BT (Mulut / Suara)", "Speaker eksternal nirkabel penerima audio Bluetooth A2DP. Menghasilkan suara musik yang jernih dan keras."),
        ("Kaset Warna (Trigger Musik)", "Kaset fisik buatan: Kaset Kuning untuk lagu 'Aku Dah Lupa' dan Kaset Merah untuk lagu 'What If'."),
        ("Kabel Micro-USB & Web Dashboard (Jalur Data)", "Menghubungkan ESP32 ke laptop. Memungkinkan siswa memantau log serial live, mengatur volume 0-100%, dan memilih lagu lewat browser.")
    ]
    for name, desc in komponen_list:
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(4)
        p.paragraph_format.space_after = Pt(4)
        r_name = p.add_run(f"• {name}: ")
        r_name.bold = True
        r_name.font.color.rgb = RGBColor(13, 148, 136)
        p.add_run(desc)

    # ==========================================
    # SLIDE 4: CARA KERJA SISTEM (STEP-BY-STEP)
    # ==========================================
    add_section_header("Cara Kerja Sistem (Alur Logika Alat)", 4)
    
    p = doc.add_paragraph()
    p.add_run("Alur Kerja dari Kaset Masuk Hingga Suara Berbunyi:\n").bold = True
    
    steps = [
        ("1. Mode Standby (Menunggu Kaset)", "Sensor TCS3000 terus memancarkan cahaya LED ke dalam slot. Saat slot kosong, cahaya memantul jauh ke dinding belakang (durasi pulsa Clear sekitar 70-86 us). Karena nilai ini > 50 us, ESP32 tahu slot sedang kosong."),
        ("2. Kaset Dimasukkan ke Slot", "Saat kaset fisik dimasukkan mendekati sensor, pantulan cahaya menjadi sangat kuat sehingga durasi pulsa Clear drop drastis di bawah 35 us (terbaca sekitar 7-14 us). ESP32 langsung mendeteksi: 'Ada objek masuk!'"),
        ("3. Stabilisasi & Verifikasi Warna (300 ms)", "ESP32 tidak langsung membunyikan musik secara terburu-buru. Sistem menunggu 300ms agar posisi kaset stabil di tangan pengguna, lalu mengambil 3 kali sampel rata-rata warna RGB:"),
        ("   a) Kaset Kuning", "Warna Merah dan Hijau sama-sama tinggi (r ≈ 0.42, g ≈ 0.37, b ≈ 0.21). ESP32 memvalidasi: 'BENAR WARNA KUNING!'. File '/akudahlupa.mp3' diputar."),
        ("   b) Kaset Merah", "Warna Merah dominan jauh melampaui Hijau dan Biru (r ≥ 0.48, g ≤ 0.30, selisih r - g ≥ 0.18). ESP32 memvalidasi: 'BENAR WARNA MERAH!'. File '/whatif.mp3' diputar."),
        ("   c) Warna Lain / Benda Asing", "Jika dimasukkan jari tangan atau kertas sembarangan, sensor mendeteksi warna tidak cocok, lagu ditolak dan TIDAK berbunyi."),
        ("4. Auto-Stop Ketika Kaset Dicabut", "Saat lagu sedang asyik berputar, sensor tetap memantau kaset secara ringan tiap 150ms. Begitu kaset dicabut dari slot, pantulan cahaya menghilang (Clear melompat ke > 70 us). Dalam waktu 150 milidetik, lagu otomatis berhenti seketika, persis seperti Jukebox Minecraft!")
    ]
    for title, desc in steps:
        p = doc.add_paragraph()
        p.paragraph_format.left_indent = Inches(0.2)
        p.paragraph_format.space_before = Pt(2)
        p.paragraph_format.space_after = Pt(2)
        p.add_run(f"{title}: ").bold = True
        p.add_run(desc)

    # ==========================================
    # SLIDE 5: DUAL CORE & PROBLEM SOLVING
    # ==========================================
    add_section_header("Trik Rekayasa: Mengatasi Suara Rusak / Patah-Patah", 5)
    
    p = doc.add_paragraph()
    p.add_run("Masalah Klasik pada Proyek Audio Mikrokontroler:\n").bold = True
    p.add_run(
        "Banyak siswa SMK mengalami suara audio Bluetooth pada ESP32 terdengar patah-patah, kresek-kresek, atau robotik. "
        "Mengapa ini terjadi? Karena ESP32 kehabisan waktu CPU! Membaca sensor warna memerlukan jeda tunggu pulsa (pulseIn). "
        "Akibatnya, pengiriman data lagu ke speaker Bluetooth terlambat sehingga buffer speaker kosong (Buffer Starvation)."
    )
    
    p_sol = doc.add_paragraph()
    p_sol.add_run("\nSolusi Cerdas di Proyek Ini (Nilai Plus di Depan Penguji):\n").bold = True
    
    solusi_items = [
        ("Pemanfaatan Dual-Core (FreeRTOS Architecture)", "ESP32 memiliki dua prosesor (Core 0 dan Core 1). Kita menugaskan Core 0 khusus melayani protokol Bluetooth tanpa gangguan, sedangkan Core 1 khusus mendekode file MP3 dan membaca sensor."),
        ("Pre-Buffering Ring Buffer (32 KiloByte)", "Sebelum lagu mulai dialirkan ke speaker, ESP32 mengisi 'tandon air data' sebesar 16 KB terlebih dahulu. Jadi ketika sensor sedang membaca warna, musik tetap mengalir lancar dari tandon buffer tanpa jeda sedikit pun."),
        ("Daya Radio Maksimum (+9 dBm)", "Daya pancar radio Bluetooth ESP32 disetel ke tingkat tertinggi (+9 dBm), membuat sinyal tahan terhadap rintangan dan jarak jauh tetap jernih seperti koneksi smartphone."),
        ("Sensor Ultra-Lightweight Saat Memutar Lagu", "Saat lagu berjalan, sensor tidak membaca semua warna RGB, melainkan HANYA membaca 1 pulsa Clear selama 6 milidetik. Ini menghemat 95% beban kerja CPU.")
    ]
    for s_title, s_desc in solusi_items:
        p = doc.add_paragraph()
        p.paragraph_format.left_indent = Inches(0.2)
        p.paragraph_format.space_before = Pt(3)
        p.paragraph_format.space_after = Pt(3)
        p.add_run(f"✔ {s_title}: ").bold = True
        p.add_run(s_desc)

    # ==========================================
    # SLIDE 6: SKEMA KABEL & PINOUT (PRAKTIKUM)
    # ==========================================
    add_section_header("Skema Rangkaian Pin Hardware (Cheatsheet Praktik)", 6)
    
    table_pin = doc.add_table(rows=1, cols=3)
    p_hdr = table_pin.rows[0].cells
    p_hdr[0].text = "Pin Sensor TCS3000"
    p_hdr[1].text = "Pin ESP32 (GPIO)"
    p_hdr[2].text = "Fungsi & Peran Sinyal"
    for c in p_hdr:
        set_cell_background(c, "1E3A8A")
        c.paragraphs[0].runs[0].font.color.rgb = RGBColor(255, 255, 255)
        c.paragraphs[0].runs[0].font.bold = True

    pin_mappings = [
        ("VCC", "VIN / 5V (atau 3.3V)", "Sumber daya listrik modul sensor optik"),
        ("GND", "GND", "Ground tegangan bersama"),
        ("S0", "GPIO 14", "Pengatur skala frekuensi keluaran (dibuat HIGH)"),
        ("S1", "GPIO 27", "Pengatur skala frekuensi keluaran (dibuat LOW = skala 20%)"),
        ("S2", "GPIO 26", "Pemilih fotodioda (Filter warna Merah/Hijau/Biru/Clear)"),
        ("S3", "GPIO 25", "Pemilih fotodioda (Filter warna Merah/Hijau/Biru/Clear)"),
        ("OUT", "GPIO 32", "Keluaran pulsa gelombang frekuensi sensor ke ESP32"),
        ("LED", "GPIO 33", "Kontrol lampu LED penerangan sensor (dibuat HIGH = menyala)")
    ]
    for p_sens, p_esp, p_fn in pin_mappings:
        row = table_pin.add_row()
        c0, c1, c2 = row.cells
        c0.text = p_sens
        c1.text = p_esp
        c2.text = p_fn
        c0.paragraphs[0].runs[0].font.bold = True
        c0.paragraphs[0].runs[0].font.size = Pt(9.5)
        c1.paragraphs[0].runs[0].font.size = Pt(9.5)
        c2.paragraphs[0].runs[0].font.size = Pt(9.5)
        set_cell_background(c0, "F3F4F6")
        set_cell_background(c1, "FFFFFF")
        set_cell_background(c2, "F9FAFB")

    # ==========================================
    # SLIDE 7: SKRIP DEMONSTRASI PRESENTASI
    # ==========================================
    add_section_header("Skrip Teks Presentasi Demo (Bisa Dibaca Saat Tampil)", 7)
    
    p = doc.add_paragraph()
    p.add_run("Tips untuk Siswa SMK: ").bold = True
    p.add_run("Bicaralah dengan tenang, percaya diri, dan sertakan gerakan tangan saat menunjukkan alat ke arah guru/penguji.\n")
    
    demo_script = [
        ("1. Pembukaan (30 detik)", 
         "'Selamat pagi Bapak/Ibu guru penguji dan teman-teman. Hari ini kelompok kami mempresentasikan proyek IoT: Smart Minecraft Jukebox berbasis ESP32. "
         "Alat ini terinspirasi dari balok pemutar kaset di game Minecraft, di mana alat bisa mengenali kaset warna secara fisik dan langsung memutar lagunya ke speaker Bluetooth.'"),
        
        ("2. Menjelaskan Komponen Utama (45 detik)",
         "'Komponen utama alat ini adalah mikrokontroler ESP32 sebagai otak pengolah data, dan sensor optik TCS3000 sebagai matanya. "
         "Keunggulan utama alat kami: kami tidak memerlukan modul kartu memori MicroSD eksternal. "
         "Lagu-lagu MP3 disimpan langsung di dalam chip memori flash ESP32 menggunakan teknologi LittleFS sebesar 2.8 MegaByte.'"),
         
        ("3. Demonstrasi Kaset Kuning (1 menit)",
         "'Sekarang kami demonstrasikan. Perhatikan speaker Bluetooth YMX-868BT sudah terkoneksi nirkabel. "
         "Saya akan memasukkan Kaset Kuning ke dalam slot. "
         "(Masukkan kaset kuning) "
         "Sensor membaca pantulan cahaya selama 300 milidetik untuk memastikan kaset stabil, lalu sistem memvalidasi warna kuning dan seketika memutar lagu 'Aku Dah Lupa'! "
         "Kini saya cabut kasetnya: "
         "(Cabut kaset) "
         "Dan dalam sepersekian detik lagu langsung berhenti otomatis.'"),

        ("4. Demonstrasi Kaset Merah (45 detik)",
         "'Selanjutnya, kami uji kaset kedua, yaitu Kaset Merah. "
         "(Masukkan kaset merah) "
         "Sensor mendeteksi dominasi spektrum warna merah dengan rasio di atas 48 persen, dan langsung menyalakan lagu berbeda, yaitu 'What If'! "
         "Saat kaset dicabut, lagu kembali berhenti dengan rapi.'"),

        ("5. Menjelaskan Dashboard Web di Laptop (30 detik)",
         "'Selain kontrol fisik, kami juga membuatkan Web Dashboard di laptop melalui localhost port 5000. "
         "Melalui antarmuka web bergaya Minecraft ini, kita bisa memantau pembacaan sensor live, mengatur volume suara 0-100 persen, dan memutar lagu secara manual.'"),

        ("6. Penutup (20 detik)",
         "'Demikian presentasi dari proyek Jukebox Minecraft kami. Sistem ini membuktikan bahwa konsep game dapat direalisasikan menjadi teknologi embedded IoT yang aplikatif dan bermanfaat. Terima kasih, kami siap menerima pertanyaan.'")
    ]
    for part, script_text in demo_script:
        p = doc.add_paragraph()
        p.paragraph_format.left_indent = Inches(0.15)
        p.paragraph_format.space_before = Pt(4)
        p.paragraph_format.space_after = Pt(4)
        p.add_run(f"{part}:\n").bold = True
        p.add_run(f'"{script_text}"').italic = True

    # ==========================================
    # SLIDE 8: FAQ & TANYA JAWAB PENGUJI SMK
    # ==========================================
    add_section_header("Bocoran Pertanyaan Penguji & Cara Menjawabnya", 8)
    
    faq_list = [
        ("Q1: Mengapa memakai LittleFS, bukan modul MicroSD Card yang umum?",
         "Jawaban SMK: 'Modul MicroSD eksternal sering mengalami kegagalan kontak fisik (longgar/kotor) saat kaset dimasukkan, dan komunikasi kabel SPI MicroSD menambah beban pin. Dengan LittleFS, data lagu tersimpan di chip flash internal ESP32, sehingga alat lebih ringkas, tahan getaran, dan biaya produksi lebih murah.'"),
        
        ("Q2: Bagaimana cara sensor membedakan Kuning dan Merah?",
         "Jawaban SMK: 'Sensor mengukur intensitas cahaya lewat filter Merah, Hijau, dan Biru. Pada warna Merah murni, komponen Merah (normR) sangat dominan (di atas 48%) sedangkan Hijau rendah. Sedangkan pada warna Kuning, komponen Merah dan Hijau sama-sama tinggi (r ≈ 0.42 dan g ≈ 0.37) karena warna kuning dalam optik adalah perpaduan sinar merah dan hijau.'"),

        ("Q3: Mengapa saat slot kosong sensor tidak mendeteksi kaset?",
         "Jawaban SMK: 'Kami menerapkan kalibrasi ambang batas pulsa (Clear Threshold) pada angka 50 mikrosekon. Ketika kaset berada di dekat sensor, pantulan cahayanya sangat kuat sehingga durasi pulsa Clear sangat cepat (< 35 us). Saat slot kosong, cahaya memantul jauh di dinding slot sehingga terbaca lebih lambat (> 70 us), sehingga sistem tidak akan tertipu oleh slot kosong.'"),

        ("Q4: Apakah sinyal Bluetooth ESP32 stabil jika dibandingkan smartphone?",
         "Jawaban SMK: 'Sangat stabil, karena kami mengaktifkan daya pancar radio ESP32 pada level maksimum (+9 dBm) melalui fungsi esp_bredr_tx_power_set, serta menerapkan sistem FreeRTOS Core 1 Task dan Pre-Buffering 32KB agar aliran audio ke speaker tidak terputus-putus.'")
    ]
    for q, a in faq_list:
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(4)
        p.paragraph_format.space_after = Pt(4)
        r_q = p.add_run(f"❓ {q}\n")
        r_q.bold = True
        r_q.font.color.rgb = RGBColor(180, 83, 9)
        p.add_run(a)

    # Save Document
    output_filename = "MATERI_PRESENTASI_JUKEBOX_MINECRAFT_ESP32.docx"
    doc.save(output_filename)
    print(f"[OK] File presentasi Word berhasil dibuat: {output_filename}")

if __name__ == "__main__":
    create_presentation_doc()
