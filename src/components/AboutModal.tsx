import React, { useState } from 'react';
import { X } from 'lucide-react';

interface AboutModalProps {
  open: boolean;
  onClose: () => void;
  lang: 'tr' | 'en';
}

export const AboutModal: React.FC<AboutModalProps> = ({ open, onClose, lang }) => {
  const [photoError, setPhotoError] = useState(false);

  if (!open) return null;

  const links = [
    { label: 'Instagram @alptugdag', href: 'https://instagram.com/alptugdag' },
    { label: 'YouTube @AlptugDag', href: 'https://youtube.com/@AlptugDag' },
    { label: 'Letterboxd', href: 'https://letterboxd.com/AlptugDag/' },
    { label: 'LinkedIn', href: 'https://www.linkedin.com/in/AlptugDag/' },
    { label: 'GitHub', href: 'https://github.com/AlptugDag' },
  ];

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label={lang === 'tr' ? 'AlptugDUB Hakkında' : 'About AlptugDUB'}
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4 backdrop-blur-sm"
      onClick={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div className="w-full max-w-2xl rounded-2xl border border-white/12 bg-[#10151F] p-6 shadow-2xl">
        <div className="grid grid-cols-1 gap-6 sm:grid-cols-[210px_1fr]">
          <div className="flex flex-col items-center">
            {!photoError ? (
              <img
                src="/assets/creator.jpg"
                alt="Alptuğ Dağ"
                referrerPolicy="no-referrer"
                onError={() => setPhotoError(true)}
                className="h-60 w-full rounded-xl object-cover border border-white/10"
              />
            ) : (
              <div className="flex h-60 w-full items-center justify-center rounded-xl border border-white/10 bg-[#171E2C] text-lg font-bold text-slate-300">
                Alptuğ Dağ
              </div>
            )}
            <img
              src="/assets/signature.svg"
              alt="Alptuğ Dağ imzası"
              referrerPolicy="no-referrer"
              className="mt-3 h-12 w-auto opacity-85"
            />
          </div>

          <div className="flex flex-col justify-between">
            <div>
              <div className="flex items-start justify-between gap-2">
                <div>
                  <h2 className="text-2xl font-extrabold tracking-tight text-slate-100">
                    Alptuğ Dağ
                  </h2>
                  <p className="mt-0.5 text-xs font-medium text-amber-400">
                    {lang === 'tr'
                      ? 'Sinemacı · Hikâye anlatıcı · Seslendirme sanatçısı'
                      : 'Filmmaker · Storyteller · Voice artist'}
                  </p>
                </div>
                <button
                  type="button"
                  onClick={onClose}
                  aria-label={lang === 'tr' ? 'Hakkında penceresini kapat' : 'Close About modal'}
                  className="rounded-lg border border-white/10 bg-[#182030] p-1.5 text-slate-300 hover:bg-[#232F44] transition-colors"
                >
                  <X className="h-4 w-4" />
                </button>
              </div>

              <p className="mt-4 text-sm leading-relaxed text-slate-300">
                {lang === 'tr'
                  ? "2000'de Ankara'da doğdu. Yönettiği kısa film Sessiz (2023) ile tanınır. AlptugDUB'ı kendi seslendirme ve dublaj işleri için tasarlayıp geliştirdi. Studio 3.0 sürümü, ses mühendisliği ilkeleriyle sıfırdan yeniden yapılandırılmış hibrit Web Audio DSP mimarisini sunar."
                  : 'Born in Ankara in 2000, known for his short film Sessiz (2023). He designed AlptugDUB for his own voice-over and dubbing work. Studio 3.0 brings a re-engineered hybrid Web Audio DSP pipeline built from audio engineering first principles.'}
              </p>

              <div className="mt-5 flex flex-wrap gap-2">
                {links.map((item) => (
                  <a
                    key={item.href}
                    href={item.href}
                    target="_blank"
                    rel="noreferrer"
                    className="rounded-lg border border-white/10 bg-[#151C28] px-3 py-1.5 text-xs font-medium text-slate-200 hover:border-amber-400/50 hover:text-amber-300 transition-colors"
                  >
                    {item.label}
                  </a>
                ))}
              </div>
            </div>

            <div className="mt-6 flex items-center justify-between border-t border-white/8 pt-3 text-[11px] text-slate-500">
              <span>AlptugDUB Studio 3.0 · GNU AGPL-3.0</span>
              <button
                type="button"
                onClick={onClose}
                className="rounded-lg bg-amber-500 px-4 py-1.5 text-xs font-semibold text-slate-950 hover:bg-amber-400 transition-colors"
              >
                {lang === 'tr' ? 'Kapat' : 'Close'}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
