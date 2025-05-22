# Report

## 1. Design Choices

Bu proje, insansız hava araçları (dronelar) ve kurtarılacak bireyler (survivorlar) arasındaki etkileşimi simüle eden çoklu iş parçacıklı bir sistemdir. Tasarım aşamasında, uygulamanın gerçek zamanlı ve senkronize biçimde çalışabilmesi için aşağıdaki kritik tasarım kararları alınmıştır:

- **2D Grid Tabanlı Harita Modeli:** Harita, map.width ve map.height ile tanımlanan hücrelerden oluşan bir grid yapısıdır ve bu hücreler CELL\_SIZE ile piksellere dönüştürülür. Örneğin, pencere boyutları aşağıdaki şekilde ayarlanır:
```c
window\_width = map.width \* CELL\_SIZE;
window\_height = map.height \* CELL\_SIZE;
```
Bu yapı, koordinat bazlı konumlandırmayı kolaylaştırmakta ve hücrelerin görsel temsilini basitleştirmektedir.

- **Modüler Kod Organizasyonu:** Projede drone.h, survivor.h, map.h, view.h gibi başlık dosyaları ile fonksiyonlar ayrı ayrı modüllerde organize edilmiştir. Böylece, kodun bakımı ve ilerideki geliştirmeleri kolaylaşmaktadır.
- **SDL2 Kütüphanesi Kullanımı:** Görselleştirme için platform bağımsız, donanım hızlandırmalı SDL2 tercih edilmiştir. SDL\_Renderer ile render işlemleri GPU hızlandırmalı olarak yapılmakta, bu da yüksek kare hızları ve akıcı animasyon sağlar:

renderer = SDL\_CreateRenderer(window, -1, SDL\_RENDERER\_ACCELERATED | SDL\_RENDERER\_PRESENTVSYNC);

- **Dinamik Veri Yapıları:** Dronelar ve survivorlar, bağlantılı listeler (Linked List) üzerinden yönetilir. Bu yapı, nesnelerin dinamik olarak eklenip çıkarılmasına olanak verir. Örneğin, draw\_drones() fonksiyonu drone listesini dolaşarak her birini çizer:
- 
```c
Node \*current = drones->head;
while (current != NULL) {
  Drone \*drone = (Drone \*)current->data;
  // drone çizim işlemi
  current = current->next;
}
```
- **Duruma Bağlı Renk Kodlama:** Droneların durumu (IDLE, ON\_MISSION, DISCONNECTED) renklerle ayrılarak kullanıcıya hızlı durum algısı sağlar. Bu tasarım, görsel geribildirim ve simülasyonun izlenebilirliği açısından önemlidir.
- **Socket Tabanlı İletişim:** Proje mimarisinde, dronelar ile merkezi kontrol sistemi arasında iletişimi sağlamak üzere TCP socket’leri kullanılmıştır. Bu yapı, farklı cihazlar arasında gerçek zamanlı veri alışverişini mümkün kılarak simülasyonun gerçekçiliğini artırır. Server-client modeliyle, sunucu (server) tarafı kontrol ve veri güncellemelerini yönetirken, client (drone) tarafları kendi durumlarını güncelleyip sunucuya iletir.
-----
## 2. Synchronization Strategy

Proje mimarisi, birden çok iş parçacığının ortak veri yapıları üzerinde eşzamanlı işlem yaptığı senaryolara dayanır. Bu yüzden veri tutarlılığı ve yarış durumlarının önlenmesi kritik önemdedir.

- **Mutex Kilitleri (pthread\_mutex):** Dronelar ve survivorlar, ortak linked list yapıları üzerinde tutulur ve bu yapılara erişim sırasında mutex kilitleri kullanılır:

```c
pthread\_mutex\_lock(&drones->lock);
// drone listesi üzerinde işlem
pthread\_mutex\_unlock(&drones->lock);
```

- **İnce Taneli Kilitleme:** Her drone nesnesi için ayrı bir mutex bulunmaktadır:

```c
pthread\_mutex\_lock(&drone->lock);
//drone verileri üzerinde işlem
pthread\_mutex\_unlock(&drone->lock);
```

Bu sayede drone listesi ve bireysel drone nesneleri ayrı ayrı korunarak, farklı thread'lerin paralel olarak farklı drone'lara erişimi mümkün olur. Kilitlerin ince taneli tutulması, performansın artırılması için kritik bir stratejidir.

- **Kilitlerin Sürekliliği ve Sırası:** Kilitler mümkün olan en kısa sürede alınır ve serbest bırakılır, böylece deadlock ve uzun kilit bekleme süreleri minimize edilir. Kilitlerin belirli bir sırayla alınması, kilit karşılıklı bekleme (deadlock) riskini azaltır.
- **Grafik ve Veri Ayrımı:** Grafik işlemleri ana thread’de yürütülürken, veri güncellemeleri başka thread’lerce yapılır. Bu ayrım, grafik güncellemeleri ile veri değişikliklerinin çakışmasını önler ve program kararlılığını artırır.
- **Socket Erişiminde Senkronizasyon:** Server ve client arasında socket üzerinden veri aktarımı sırasında, aynı socket kaynaklarına erişim thread-safe şekilde kontrol edilir. Örneğin, gelen/verilen mesajlar mutex ile korunur veya asenkron I/O teknikleri kullanılır. Böylece veri çakışmaları engellenir ve iletişim güvenilirliği sağlanır.
-----
## 3. Performance Analysis

Projede performans, gerçek zamanlı simülasyonun gerektirdiği akıcılık ve veri tutarlılığı dengesi gözetilerek optimize edilmiştir.

- **Donanım Hızlandırmalı Render:** SDL2’nin SDL\_RENDERER\_ACCELERATED ve SDL\_RENDERER\_PRESENTVSYNC bayraklarıyla oluşturulan renderer, grafik işlemlerinin GPU tarafından hızlandırılmasını ve dikey senkronizasyon ile görüntü yırtılmalarının önlenmesini sağlar. Bu, özellikle 30x20 gibi yüksek hücre sayılarında render performansını artırır.
- **Paralel İşlem ve Kilitlerin İnceliği:** Kilitlerin ince taneli uygulanması ve kilit sürelerinin kısa tutulması, çoklu thread’lerin birbirini beklemeden çalışmasını sağlar. Bu mimari, CPU kaynaklarının etkin kullanılmasına olanak tanır.
- **Dinamik Veri Yapısı Performansı:** Bağlantılı listeler dinamik ekleme ve çıkarma işlemlerini kolaylaştırsa da, çok yüksek eleman sayılarında arama ve gezinme işlemleri maliyetli olabilir. Bu durum, yüksek sayıda drone ve survivor varlığında performans darboğazı oluşturabilir.
- **Socket Haberleşme Gecikmeleri:** TCP socket tabanlı iletişimde, ağ gecikmeleri ve paket iletim süreleri uygulamanın gerçek zamanlılığı üzerinde doğrudan etkilidir. Bu proje kapsamında, iletişim mesajlarının boyutu küçük tutulmuş ve bloklama yapmayan socket işlemleri tercih edilerek bu gecikmeler minimize edilmiştir.
- **Geliştirme İmkânları:** Performans daha da artırılmak istenirse:
  - Lock-free veri yapıları veya atomik işlemler kullanılabilir.
  - Bölgesel güncellemeler (dirty rectangles) ile sadece değişen alanların render edilmesi sağlanabilir.
  - Daha hızlı arama için hash tablolar veya ağaç yapıları tercih edilebilir.
  - Socket iletişimi için UDP protokolü veya mesaj sıralaması ve yeniden iletim mekanizmaları geliştirilebilir.

Sonuç olarak, mevcut performans gerçek zamanlı simülasyon için yeterli olup, kullanıcı etkileşimlerinde gecikme ve takılma gözlemlenmemektedir.




