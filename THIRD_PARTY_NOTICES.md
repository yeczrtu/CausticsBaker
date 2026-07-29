# Third-Party Notices

CausticsBaker is distributed under the MIT License found in [`LICENSE`](LICENSE).
That license applies only to the original CausticsBaker code and documentation.
The following components and products retain their own terms.

## CustomRaytracingShader

The material ray-tracing integration in CausticsBaker was based in part on
[historia-Inc/CustomRaytracingShader](https://github.com/historia-Inc/CustomRaytracingShader).
Its copyright and MIT license are reproduced below.

> MIT License
>
> Copyright (c) 2026 historia
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Intel Open Image Denoise

The optional bake denoiser uses the `IntelOIDN` external module supplied by the
user's Unreal Engine installation. Open Image Denoise is not vendored in this
repository or in the CausticsBaker plugin package. Unreal Engine 5.8 currently
supplies Open Image Denoise 2.3.1 under the
[Apache License 2.0](https://github.com/RenderKit/oidn/blob/master/LICENSE.txt).

Source: [RenderKit/oidn](https://github.com/RenderKit/oidn)

## Unreal Engine

Unreal Engine source code and binaries are not included in this repository.
Building or using this plugin requires a separately licensed Unreal Engine 5.8
installation and remains subject to the
[Unreal Engine End User License Agreement](https://www.unrealengine.com/eula/unreal).
The MIT License for CausticsBaker does not grant rights to Epic Games software,
content, or trademarks.

CausticsBaker uses Unreal® Engine. Unreal® is a trademark or registered
trademark of Epic Games, Inc. in the United States of America and elsewhere.

Unreal® Engine, Copyright 1998-2026, Epic Games, Inc. All rights reserved.
