const CACHE_NAME = 'space-unlimited-android-v1';
const ASSETS_TO_CACHE = [
  './',
  './index.html',
  './manifest.json',
  './css/style.css',
  './js/gfx-data.js',
  './js/constants.js',
  './js/audio.js',
  './js/save.js',
  './js/touch-controls.js',
  './js/starfield.js',
  './js/shop.js',
  './js/game.js',
  './js/app.js',
  './assets/audio/menu.wav',
  './assets/audio/game.wav',
  './assets/audio/laser.wav',
  './assets/audio/explosion.wav',
  './assets/audio/pickup.wav',
  './assets/images/icon-192.png',
  './assets/images/icon-512.png',
  './assets/images/classic-ship.png',
  './assets/images/asteroid-large.png',
  './assets/images/asteroid-medium-a.png',
  './assets/images/asteroid-medium-b.png',
  './assets/images/asteroid-small.png',
  './assets/images/asteroid-tiny.png',
  './assets/images/laser.png',
  './assets/images/shield.png',
  './assets/images/starfield.png'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      return cache.addAll(ASSETS_TO_CACHE).catch((err) => {
        console.warn('Pre-caching assets warning:', err);
      });
    }).then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) => {
      return Promise.all(
        keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))
      );
    }).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (event) => {
  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      if (cachedResponse) {
        return cachedResponse;
      }
      return fetch(event.request).then((response) => {
        if (!response || response.status !== 200 || response.type !== 'basic') {
          return response;
        }
        const responseToCache = response.clone();
        caches.open(CACHE_NAME).then((cache) => {
          cache.put(event.request, responseToCache);
        });
        return response;
      });
    }).catch(() => caches.match('./index.html'))
  );
});
