const CACHE_NAME = 'kvm-dongle-v1';
const ASSETS = [
  '/kvm-dongle-usb/',
  '/kvm-dongle-usb/index.html',
  '/kvm-dongle-usb/manifest.json',
  '/kvm-dongle-usb/icon-192.png',
  '/kvm-dongle-usb/icon-512.png'
];

// Install: cache all assets
self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then(cache => cache.addAll(ASSETS))
      .then(() => self.skipWaiting())
  );
});

// Activate: clean old caches
self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(keys => {
      return Promise.all(
        keys.filter(k => k !== CACHE_NAME).map(k => caches.delete(k))
      );
    }).then(() => clients.claim())
  );
});

// Fetch: serve from cache, fallback to network
self.addEventListener('fetch', event => {
  event.respondWith(
    caches.match(event.request)
      .then(cached => cached || fetch(event.request))
      .catch(() => {
        // Offline fallback: return index.html for navigation requests
        if (event.request.mode === 'navigate') {
          return caches.match('/kvm-dongle-usb/');
        }
        return new Response('Offline', { status: 503 });
      })
  );
});