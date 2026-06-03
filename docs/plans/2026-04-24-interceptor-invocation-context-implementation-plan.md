# Interceptor Invocation Context Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a first Frida-like invocation context for native hook callbacks so `onEnter` and `onLeave` share one `this` object carrying minimal call metadata and register snapshots.

**Architecture:** Extend the native hook event with one `invocation_id` plus lightweight call metadata captured on the hook thread. In the JS runtime, create one invocation object on `onEnter`, store it until `onLeave`, and use it as the callback receiver so script-side state survives between phases.

**Tech Stack:** C++, QuickJS, existing Nook inline-hook bridge and runtime callback dispatch.

---
