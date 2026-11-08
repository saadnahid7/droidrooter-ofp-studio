// SPDX-License-Identifier: GPL-3.0-only
use crate::Patched;
use std::{
    ffi::{CStr, OsString},
    os::windows::ffi::OsStringExt,
    path::PathBuf,
    sync::atomic::{AtomicBool, Ordering},
};
pub struct Cancellation(AtomicBool);
fn error(out: *mut u8, cap: usize, text: &str) {
    if out.is_null() || cap == 0 {
        return;
    }
    let n = text.len().min(cap - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(text.as_ptr(), out, n);
        *out.add(n) = 0;
    }
}
#[unsafe(no_mangle)]
pub extern "C" fn dr_cancel_new() -> *mut Cancellation {
    Box::into_raw(Box::new(Cancellation(AtomicBool::new(false))))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dr_cancelled(c: *const Cancellation) -> i32 {
    unsafe { c.as_ref() }.is_some_and(|c| c.0.load(Ordering::Relaxed)) as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dr_cancel_set(c: *const Cancellation) {
    if let Some(c) = unsafe { c.as_ref() } {
        c.0.store(true, Ordering::Relaxed);
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dr_cancel_free(c: *mut Cancellation) {
    if !c.is_null() {
        drop(unsafe { Box::from_raw(c) });
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dr_patch(
    stock: *const u8,
    len: usize,
    apk: *const u16,
    apk_len: usize,
    preinit: *const std::ffi::c_char,
    cancel: *const Cancellation,
    error_out: *mut u8,
    error_cap: usize,
) -> *mut Patched {
    if stock.is_null()
        || apk.is_null()
        || preinit.is_null()
        || cancel.is_null()
        || len > 134_217_728
        || apk_len > 32767
    {
        error(error_out, error_cap, "Invalid patch request");
        return std::ptr::null_mut();
    }
    let result = std::panic::catch_unwind(|| {
        let stock = unsafe { std::slice::from_raw_parts(stock, len) };
        let apk = PathBuf::from(OsString::from_wide(unsafe {
            std::slice::from_raw_parts(apk, apk_len)
        }));
        let preinit = unsafe { CStr::from_ptr(preinit) }.to_str()?;
        crate::patch(stock, &apk, preinit, &unsafe { &*cancel }.0)
    });
    match result {
        Ok(Ok(p)) => Box::into_raw(Box::new(p)),
        Ok(Err(e)) => {
            error(error_out, error_cap, &format!("{e:#}"));
            std::ptr::null_mut()
        }
        Err(_) => {
            error(
                error_out,
                error_cap,
                "Native patch engine rejected the input",
            );
            std::ptr::null_mut()
        }
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dr_result_bytes(p: *const Patched, n: *mut usize) -> *const u8 {
    if let Some(p) = unsafe { p.as_ref() } {
        unsafe {
            *n = p.bytes.len();
        }
        p.bytes.as_ptr()
    } else {
        std::ptr::null()
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dr_result_report(p: *const Patched, n: *mut usize) -> *const u8 {
    if let Some(p) = unsafe { p.as_ref() } {
        unsafe {
            *n = p.report.len();
        }
        p.report.as_ptr()
    } else {
        std::ptr::null()
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dr_result_free(p: *mut Patched) {
    if !p.is_null() {
        drop(unsafe { Box::from_raw(p) });
    }
}
