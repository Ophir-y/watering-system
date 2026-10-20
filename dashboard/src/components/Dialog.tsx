import { useEffect, useRef, type ReactNode } from 'react';

// Native <dialog>: focus trapping, Escape and the backdrop come for free.
export function Dialog({
  open,
  onClose,
  children,
  labelledBy,
}: {
  open: boolean;
  onClose: () => void;
  children: ReactNode;
  labelledBy?: string;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const d = ref.current;
    if (!d) return;
    if (open && !d.open) d.showModal?.();
    if (!open && d.open) d.close();
  }, [open]);
  return (
    <dialog ref={ref} className="modal" onClose={onClose} aria-labelledby={labelledBy}>
      {open && children}
    </dialog>
  );
}
